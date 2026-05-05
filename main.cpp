#include "SimCpu.h"
#include "PhysMemory.h"
#include "RISCV.h"
#include "config.h"
#include "diff.h"
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>
#include <unistd.h>
#include "RISCV.h"

#ifndef MAX_COMMIT_INST
#define MAX_COMMIT_INST 15000000000ULL
#endif

using namespace std;
extern RefCpu ref_cpu;

// 1. 定义配置结构
struct SimConfig {
  enum Mode {
    RUN,     // 运行模式：从头运行二进制文件 (乱序)
    CKPT,    // 快照模式：从快照恢复
    FAST,    // 快速模式：先单周期快进，再乱序执行
    REF_ONLY // 仅运行 Reference Model
  } mode = RUN;

  // 目标文件路径
  std::string target_file;
  // 存储 Fast-forward 的指令数/周期数
  uint64_t fast_forward_count = 0;
  // CKPT 模式下，O3 目标 warmup 步数（0~checkpoint interval）
  uint64_t ckpt_warmup_target = 0;
  bool ckpt_warmup_target_set = false;
  uint64_t max_commit_inst = static_cast<uint64_t>(MAX_COMMIT_INST);
  bool max_commit_inst_set = false;
};

// 2. 帮助信息更新
void print_help(char *argv[]) {
  std::cout << "Usage: " << argv[0] << " [options] <target_file>" << std::endl;
  std::cout << "\nOptions:" << std::endl;
  std::cout
      << "  -m, --mode <run|ckpt|fast|ref>  Set execution mode (default: run)"
      << std::endl;
  std::cout << "  -f, --fast-forward <num>    Number of cycles/insts to "
               "fast-forward (only for fast mode)"
            << std::endl;
  std::cout << "  -w, --warmup <num>  In CKPT mode, target O3 "
               "warmup steps in [0, checkpoint_interval] "
               "(default: checkpoint_interval)"
            << std::endl;
  std::cout
      << "  -c, --max-commit <num>  Stop after <num> committed instructions "
         "(default: checkpoint_interval in CKPT mode, compile-time "
         "MAX_COMMIT_INST otherwise)"
      << std::endl;
  std::cout << "  -h, --help                  Show this message" << std::endl;
  std::cout << "\nExamples:" << std::endl;
  std::cout << "  Run Binary: " << argv[0] << " spec_mem/mcf.bin" << std::endl;
  std::cout << "  Run Ckpt:   " << argv[0]
            << " --mode ckpt checkpoint/mcf/ckpt_sp1.gz" << std::endl;
  std::cout << "  Fast Mode:  " << argv[0]
            << " --mode fast -f 1000000 spec_mem/mcf.bin" << std::endl;
  std::cout << "  Ref Only:   " << argv[0] << " --mode ref spec_mem/mcf.bin"
            << std::endl;
}

long long sim_time = 0;
SimCpu cpu;

namespace {
volatile std::sig_atomic_t g_sigint_requested = 0;

void handle_sigint(int signo) {
  if (signo != SIGINT) {
    return;
  }

  if (g_sigint_requested != 0) {
    const char msg[] = "\n[sim] SIGINT received again, waiting for pending "
                       "dump to finish...\n";
    const ssize_t rc = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)rc;
    return;
  }

  g_sigint_requested = 1;
  const char msg[] = "\n[sim] SIGINT received, dumping LSU/MemSubsystem state "
                     "before exit...\n";
  const ssize_t rc = write(STDERR_FILENO, msg, sizeof(msg) - 1);
  (void)rc;
}

void install_signal_handlers() {
  struct sigaction sa{};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
}

bool handle_pending_sigint() {
  if (g_sigint_requested == 0) {
    return false;
  }

#if defined(LOG_ENABLE) && defined(LOG_LSU_MEM_ENABLE)
  std::cout << "[sim] SIGINT observed at cycle " << std::dec << sim_time
            << ", printing debug dump." << std::endl;
  // deadlock_debug::dump_all();
#endif
  cpu.ctx.perf.perf_print();
  return true;
}
} // namespace

void exit_handler() {
  if (cpu.ctx.exit_reason == ExitReason::NONE) {
    return;
  }
  std::cout << "\033[38;5;34m-----------------------------\033[0m" << std::endl;
  std::cout << "Simulation Exited. Printing Perf Stats..." << std::endl;
  cpu.ctx.perf.perf_print();
  std::cout << "\033[38;5;34m-----------------------------\033[0m" << std::endl;
}

int main(int argc, char *argv[]) {
  atexit(exit_handler);
  install_signal_handlers();

  SimConfig config;

  // 长参数定义
  static struct option long_options[] = {
      {"mode", required_argument, 0, 'm'},
      {"fast-forward", required_argument, 0, 'f'}, // 快进参数
      {"warmup", required_argument, 0, 'w'},
      {"max-commit", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // --- A. 解析命令行参数 ---
  while ((opt = getopt_long(argc, argv, "m:f:w:c:h", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'm': {
      std::string m(optarg);
      if (m == "ckpt") {
        config.mode = SimConfig::CKPT;
      } else if (m == "run") {
        config.mode = SimConfig::RUN;
      } else if (m == "fast") {
        config.mode = SimConfig::FAST;
      } else if (m == "ref") {
        config.mode = SimConfig::REF_ONLY;
      } else {
        std::cerr << "Error: Unknown mode '" << m
                  << "'. Use 'run', 'ckpt', 'fast', or 'ref'." << std::endl;
        return 1;
      }
      break;
    }
    case 'f': {
      try {
        config.fast_forward_count = std::stoull(optarg);
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --fast-forward: " << optarg
                  << std::endl;
        return 1;
      }
      break;
    }
    case 'w': {
      std::string warmup_arg(optarg);
      if (!warmup_arg.empty() && warmup_arg[0] == '-') {
        std::cerr << "Error: --warmup must be >= 0, got: " << optarg
                  << std::endl;
        return 1;
      }
      try {
        config.ckpt_warmup_target = std::stoull(optarg);
        config.ckpt_warmup_target_set = true;
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --warmup: " << optarg
                  << std::endl;
        return 1;
      }
      break;
    }
    case 'c': {
      std::string limit_arg(optarg);
      if (!limit_arg.empty() && limit_arg[0] == '-') {
        std::cerr << "Error: --max-commit must be a positive integer, got: "
                  << optarg << std::endl;
        return 1;
      }
      try {
        config.max_commit_inst = std::stoull(optarg);
        config.max_commit_inst_set = true;
        if (config.max_commit_inst == 0) {
          std::cerr << "Error: --max-commit must be > 0, got: 0" << std::endl;
          return 1;
        }
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --max-commit: " << optarg
                  << std::endl;
        return 1;
      }
      break;
    }
    case 'h':
      print_help(argv);
      return 0;
    default:
      print_help(argv);
      return 1;
    }
  }

  // --- B. 解析位置参数 (Target File) ---
  if (optind < argc) {
    config.target_file = argv[optind];
    if (optind + 1 < argc) {
      std::cerr
          << "Warning: Multiple files provided. Only using the first one: "
          << config.target_file << std::endl;
    }
  } else {
    std::cerr << "Error: Missing target file argument." << std::endl;
    print_help(argv);
    return 1;
  }

  // 快速模式逻辑校验
  if (config.mode == SimConfig::FAST) {
    if (config.fast_forward_count == 0) {
      std::cerr << "Error: FAST mode requires a positive number for "
                   "--fast-forward (-f)."
                << std::endl;
      return 1;
    }
  } else {
    // 如果不是快速模式，却指定了 -f，则报错
    if (config.fast_forward_count > 0) {
      std::cerr << "Warning: --fast-forward (-f) is ignored in RUN/CKPT mode."
                << std::endl;
    }
  }
  if (config.mode != SimConfig::CKPT && config.ckpt_warmup_target_set) {
    std::cerr << "Warning: --warmup (-w) is ignored unless in CKPT "
                 "mode."
              << std::endl;
  }

  if (!pmem_init()) {
    std::cerr << "Error: Failed to allocate memory!" << std::endl;
    exit(1);
  }
  cpu.init();

  // --- D. 模拟器启动逻辑 ---
  if (config.mode == SimConfig::RUN) {
    std::cout << "[Mode] RUN: Loading Binary Image..." << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.load_image(config.target_file);
  } else if (config.mode == SimConfig::CKPT) {
    std::cout << "[Mode] CKPT: Restoring from Snapshot..." << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.restore_checkpoint(config.target_file);
#ifndef CONFIG_BPU
    std::cout << "[Oracle] Synced to checkpoint snapshot in Step 0."
              << std::endl;
#endif

    const uint64_t ckpt_interval = cpu.back.ckpt_interval_inst_count;
    if (ckpt_interval == 0) {
      std::cerr << "Error: checkpoint interval must be > 0." << std::endl;
      return 1;
    }

    if (!config.max_commit_inst_set) {
      config.max_commit_inst = ckpt_interval;
    }
    if (!config.ckpt_warmup_target_set) {
      config.ckpt_warmup_target = ckpt_interval;
    }
    if (config.ckpt_warmup_target > ckpt_interval) {
      std::cerr << "Error: --warmup must be in [0," << ckpt_interval
                << "], got: " << config.ckpt_warmup_target << std::endl;
      return 1;
    }

    std::cout << "[CONFIG] checkpoint_interval = " << std::dec
              << ckpt_interval << std::endl;
    std::cout << "[CONFIG] warmup_target = " << config.ckpt_warmup_target
              << (config.ckpt_warmup_target_set ? " (runtime override)"
                                                : " (checkpoint default)")
              << std::endl;
    std::cout << "[CONFIG] max_commit_inst = " << config.max_commit_inst
              << (config.max_commit_inst_set ? " (runtime override)"
                                             : " (checkpoint default)")
              << std::endl;

    uint64_t warmup_target = config.ckpt_warmup_target;
    uint64_t ref_prewarm_target = ckpt_interval - warmup_target;

    uint64_t ref_prewarm_done = 0;
    if (ref_prewarm_target > 0) {
      std::cout << "[Run] Ref prewarm for " << ref_prewarm_target
                << " steps..." << std::endl;
      ref_cpu.uart_print = false;
      ref_cpu.ref_only = true;
      for (; ref_prewarm_done < ref_prewarm_target; ref_prewarm_done++) {
        difftest_step(false);
        if (ref_cpu.sim_end) {
          cpu.ctx.exit_reason =
              (ref_cpu.Instruction == INST_WFI) ? ExitReason::WFI
                                                : ExitReason::EBREAK;
          break;
        }
      }
      ref_cpu.ref_only = false;
      std::cout << "[Run] Ref prewarm done: " << ref_prewarm_done
                << " steps." << std::endl;
    }

    std::cout << "[Run] Restoring DUT from ref snapshot..." << std::endl;
    cpu.back.restore_from_ref();
#ifndef CONFIG_BPU
    std::cout << "[Oracle] Re-synced with ref snapshot together with DUT."
              << std::endl;
#endif
    cpu.restore_pc(cpu.back.number_PC); // 强制同步前端 PC

    cpu.ctx.is_ckpt = true;
    cpu.ctx.ckpt_warmup_commit_target = warmup_target;
    cpu.ctx.ckpt_measure_commit_target = config.max_commit_inst;
    if (cpu.ctx.ckpt_warmup_commit_target == 0) {
      cpu.ctx.perf.perf_reset();
      cpu.ctx.perf.perf_start = true;
      std::cout << "[Plan] O3 warmup skipped: target = 0. Measure phase "
                   "starts immediately."
                << std::endl;
      std::cout << "[Run] O3 measure running: target = "
                << cpu.ctx.ckpt_measure_commit_target << " committed instructions."
                << std::endl;
    } else {
      std::cout << "[Plan] O3 warmup scheduled: target = "
                << cpu.ctx.ckpt_warmup_commit_target
                << " committed instructions." << std::endl;
      std::cout << "[Plan] O3 measure scheduled: target = "
                << cpu.ctx.ckpt_measure_commit_target
                << " committed instructions after warmup." << std::endl;
      std::cout << "[Run] Entering O3 run loop. Warmup/measure transition will "
                   "be printed when warmup actually finishes."
                << std::endl;
    }
  } else if (config.mode == SimConfig::FAST) {
    std::cout << "[Mode] FAST: Hybrid Execution Strategy" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    std::cout << "[Step 1] Fast-forwarding (Simple CPU) for "
              << config.fast_forward_count << " cycles..." << std::endl;

    cpu.back.load_image(config.target_file);
    ref_cpu.uart_print = true;
    ref_cpu.ref_only = true;

    for (uint64_t i = 0; i < config.fast_forward_count; i++) {
      difftest_step(false);
      if (ref_cpu.sim_end) {
        cpu.ctx.exit_reason =
            (ref_cpu.Instruction == INST_WFI) ? ExitReason::WFI
                                              : ExitReason::EBREAK;
        break;
      }
    }
    ref_cpu.ref_only = false;

    if (cpu.ctx.exit_reason == ExitReason::NONE) {
      cpu.back.restore_from_ref();
#ifndef CONFIG_BPU
      std::cout << "[Oracle] Synced with ref snapshot before switching to O3."
                << std::endl;
#endif
      cpu.restore_pc(cpu.back.number_PC); // 强制同步前端 PC
      ref_cpu.uart_print = false;

      std::cout << "[Step 2] Run O3 CPU ... " << endl;
    }
  } else if (config.mode == SimConfig::REF_ONLY) {
    std::cout << "[Mode] REF_ONLY: Reference Model Validation" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.load_image(config.target_file);
    ref_cpu.uart_print = true;
    ref_cpu.ref_only = true;

    std::cout << "[Debug] Running Reference Model Standalone..." << std::endl;

    uint64_t ref_commit_cnt = 0;

    sim_time = 0;
    while (sim_time < (long long)MAX_SIM_TIME) { // Or a large limit
      difftest_step(false);
      ref_commit_cnt++;
      sim_time++;
      if (handle_pending_sigint()) {
        pmem_release();
        return 130;
      }
      if (ref_commit_cnt >= config.max_commit_inst) {
        cpu.ctx.exit_reason = ExitReason::SIMPOINT;
        std::cout << "[sim][REF] Reached MAX_COMMIT_INST=" << std::dec
                  << config.max_commit_inst << std::endl;
        break;
      }
      if (ref_cpu.sim_end) {
        cpu.ctx.exit_reason =
            (ref_cpu.Instruction == INST_WFI) ? ExitReason::WFI
                                              : ExitReason::EBREAK;
        break;
      }
      if (sim_time % 10000000 == 0) {
        cout << dec << sim_time << endl;
      }
    }
    std::cout << "[Debug] Ref Model Run Completed." << std::endl;
    pmem_release();
    return 0;
  }

  // 主循环
  if (cpu.ctx.exit_reason == ExitReason::NONE) {
    for (sim_time = 0; sim_time < (long long)MAX_SIM_TIME; sim_time++) {
      if (sim_time % 10000000 == 0) {
        cout << dec << sim_time << endl;
      }
      BE_LOG("************************************************************** "
             "cycle: %lld "
             "*************************************************************",
             (long long)sim_time);

      cpu.cycle();

    if (handle_pending_sigint()) {
        pmem_release();
        return 130;
      }

    if (cpu.ctx.perf.commit_num >= config.max_commit_inst) {
        cpu.ctx.exit_reason = ExitReason::SIMPOINT;
        std::cout << "[sim] Reached MAX_COMMIT_INST=" << std::dec
                  << config.max_commit_inst << std::endl;
      }

    if (cpu.ctx.exit_reason != ExitReason::NONE) {
        break;
      }
    }
  }

  pmem_release();

  if (sim_time != MAX_SIM_TIME) {
    cout << "\033[38;5;34m-----------------------------\033[0m" << endl;
    cout << "\033[38;5;34mSuccess!!!!\033[0m" << endl;
    cout << "\033[38;5;34m-----------------------------\033[0m" << endl;

    if (cpu.ctx.exit_reason == ExitReason::EBREAK) {
      uint32_t a0 = cpu.get_reg(10);
      if (a0 != 0) {
        std::cerr << "\033[1;31m[sim] Program exited with non-zero code: " << a0
                  << "\033[0m" << std::endl;
      }
      return a0;
    }
    if (cpu.ctx.exit_reason == ExitReason::WFI) {
      std::cout << "[sim] Program exited by WFI." << std::endl;
      return 0;
    }

  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

  return 0;
}
