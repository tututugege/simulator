#include "diff.h"
#include "DeadlockDebug.h"
#include "SimCpu.h"
#include "config.h"
#include "host_profile.h"
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>
#include <unistd.h>

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
  // CKPT 模式下，O3 目标 warmup 步数（0~WARMUP，默认 1000 万）
  uint64_t ckpt_warmup_target = 10000000ULL;
  bool ckpt_warmup_target_set = false;
  // 运行模式下的提交指令上限；0 表示不限制。
  uint64_t max_commit_inst = MAX_COMMIT_INST;
  // 周期进度打印间隔；0 表示关闭。
  uint64_t progress_interval = 10000000ULL;
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
               "warmup steps in [0,100000000] (default: 10000000)"
            << std::endl;
  std::cout << "  -c, --max-commit <num>  Stop after <num> committed instructions"
            << " in O3 modes (0 = no limit, default: " << MAX_COMMIT_INST << ")"
            << std::endl;
  std::cout << "  -p, --progress-interval <num>  Print progress every <num> cycles"
            << " (0 = disable, default: 10000000)" << std::endl;
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

#ifndef CONFIG_DIFF_FOCUS_ADDR_BEGIN
#define CONFIG_DIFF_FOCUS_ADDR_BEGIN 0u
#endif

#ifndef CONFIG_DIFF_FOCUS_ADDR_END
#define CONFIG_DIFF_FOCUS_ADDR_END 0u
#endif

void maybe_dump_focus_line(const char *tag) {
  if (CONFIG_DIFF_FOCUS_ADDR_END <= CONFIG_DIFF_FOCUS_ADDR_BEGIN) {
    return;
  }
  difftest_dump_memory_line(tag, CONFIG_DIFF_FOCUS_ADDR_BEGIN);
}

void handle_sigint(int signo) {
  if (signo != SIGINT) {
    return;
  }

  if (g_sigint_requested != 0) {
    const char msg[] =
        "\n[sim] SIGINT received again, waiting for pending dump to finish...\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    return;
  }

  g_sigint_requested = 1;
  const char msg[] =
      "\n[sim] SIGINT received, dumping LSU/MemSubsystem state before exit...\n";
  (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

void install_signal_handlers() {
  struct sigaction sa {};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
}

bool handle_pending_sigint() {
  if (g_sigint_requested == 0) {
    return false;
  }

#if SIM_LSU_MEM_DEBUG_PRINT
  std::cout << "[sim] SIGINT observed at cycle " << std::dec << sim_time
            << ", printing debug dump." << std::endl;
  deadlock_debug::dump_all();
#endif
  cpu.ctx.perf.perf_print();
  return true;
}

void maybe_print_o3_progress(const SimConfig &config, uint64_t &next_progress_cycle,
                             uint64_t &last_progress_cycle,
                             uint64_t &last_progress_commit) {
  if (config.progress_interval == 0) {
    return;
  }

  const uint64_t cur_cycle = cpu.ctx.perf.cycle;
  if (cur_cycle < next_progress_cycle) {
    return;
  }

  const uint64_t cur_commit = cpu.ctx.perf.commit_num;
  const uint64_t window_cycles = cur_cycle - last_progress_cycle;
  const uint64_t window_commit = cur_commit - last_progress_commit;
  const double ipc_total =
      (cur_cycle == 0) ? 0.0
                       : static_cast<double>(cur_commit) /
                             static_cast<double>(cur_cycle);
  const double ipc_window =
      (window_cycles == 0) ? 0.0
                           : static_cast<double>(window_commit) /
                                 static_cast<double>(window_cycles);

  std::cerr << "[Progress] cycle=" << std::dec << cur_cycle
            << " commit=" << cur_commit
            << " ipc_total=" << ipc_total
            << " ipc_window=" << ipc_window << std::endl;

  last_progress_cycle = cur_cycle;
  last_progress_commit = cur_commit;
  while (next_progress_cycle <= cur_cycle) {
    next_progress_cycle += config.progress_interval;
  }
}
} // namespace

void exit_handler() {
  if (cpu.ctx.exit_reason == ExitReason::NONE) {
    return;
  }
  std::cout << "\033[38;5;34m-----------------------------\033[0m" << std::endl;
  std::cout << "Simulation Exited. Printing Perf Stats..." << std::endl;
  cpu.ctx.perf.perf_print();
  frontend_host_profile::print_summary();
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
      {"progress-interval", required_argument, 0, 'p'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // --- A. 解析命令行参数 ---
  while ((opt = getopt_long(argc, argv, "m:f:w:c:p:h", long_options,
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
        std::cerr << "Error: --warmup must be in [0," << WARMUP
                  << "], got: " << optarg << std::endl;
        return 1;
      }
      try {
        config.ckpt_warmup_target = std::stoull(optarg);
        config.ckpt_warmup_target_set = true;
        if (config.ckpt_warmup_target > (uint64_t)WARMUP) {
          std::cerr << "Error: --warmup must be in [0," << WARMUP
                    << "], got: " << config.ckpt_warmup_target << std::endl;
          return 1;
        }
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --warmup: " << optarg
                  << std::endl;
        return 1;
      }
      break;
    }
    case 'c': {
      try {
        config.max_commit_inst = std::stoull(optarg);
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --max-commit: " << optarg
                  << std::endl;
        return 1;
      }
      break;
    }
    case 'p': {
      try {
        config.progress_interval = std::stoull(optarg);
      } catch (const std::exception &e) {
        std::cerr << "Error: Invalid number for --progress-interval: " << optarg
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

  p_memory = (uint32_t *)calloc(PHYSICAL_MEMORY_LENGTH, sizeof(uint32_t));
  if (!p_memory) {
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

    uint64_t warmup_target = config.ckpt_warmup_target;
    uint64_t ref_prewarm_target = (uint64_t)WARMUP - warmup_target;

    uint64_t ref_prewarm_done = 0;
    if (ref_prewarm_target > 0) {
      std::cout << "[Step 1] Ref prewarm for " << ref_prewarm_target
                << " steps..." << std::endl;
      ref_cpu.uart_print = false;
      for (; ref_prewarm_done < ref_prewarm_target; ref_prewarm_done++) {
        difftest_step(false);
        if (ref_cpu.sim_end) {
          cpu.ctx.exit_reason = ExitReason::EBREAK;
          break;
        }
      }
      std::cout << "[Step 1] Ref prewarm done: " << ref_prewarm_done
                << " steps." << std::endl;
    }

    std::cout << "[Step 2] Restore DUT from ref snapshot..." << std::endl;
    maybe_dump_focus_line("ckpt_before_restore");
    cpu.back.restore_from_ref();
    maybe_dump_focus_line("ckpt_after_restore");
    cpu.sync_mmio_devices_from_backing();
    cpu.ctx.special_timer_value = difftest_get_oracle_timer();
    cpu.reinit_frontend_after_restore();
#ifndef CONFIG_BPU
    std::cout << "[Oracle] Re-synced with ref snapshot together with DUT in "
                 "Step 2."
              << std::endl;
#endif
    cpu.restore_pc(cpu.back.number_PC); // 强制同步前端 PC
    maybe_dump_focus_line("ckpt_after_frontend_reset");

    cpu.ctx.is_ckpt = true;
    cpu.ctx.ckpt_warmup_commit_target = warmup_target;
    if (cpu.ctx.ckpt_warmup_commit_target == 0) {
      cpu.ctx.perf.perf_reset();
      cpu.ctx.perf.perf_start = true;
    }
    std::cout << "[Step 3] O3 warmup target = "
              << cpu.ctx.ckpt_warmup_commit_target << " steps." << std::endl;
  } else if (config.mode == SimConfig::FAST) {
    std::cout << "[Mode] FAST: Hybrid Execution Strategy" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    std::cout << "[Step 1] Fast-forwarding (Simple CPU) for "
              << config.fast_forward_count << " cycles..." << std::endl;

    cpu.back.load_image(config.target_file);
    ref_cpu.uart_print = true;

    for (uint64_t i = 0; i < config.fast_forward_count; i++) {
      difftest_step(false);
    }

    maybe_dump_focus_line("fast_before_restore");
    cpu.back.restore_from_ref();
    maybe_dump_focus_line("fast_after_restore");
    cpu.sync_mmio_devices_from_backing();
    cpu.ctx.special_timer_value = difftest_get_oracle_timer();
    cpu.reinit_frontend_after_restore();
#ifndef CONFIG_BPU
    std::cout << "[Oracle] Synced with ref snapshot before switching to O3."
              << std::endl;
#endif
    cpu.restore_pc(cpu.back.number_PC); // 强制同步前端 PC
    maybe_dump_focus_line("fast_after_frontend_reset");
    ref_cpu.uart_print = false;

    std::cout << "[Step 2] Run O3 CPU ... " << endl;
  } else if (config.mode == SimConfig::REF_ONLY) {
    std::cout << "[Mode] REF_ONLY: Reference Model Validation" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.load_image(config.target_file);
    ref_cpu.uart_print = true;

    std::cout << "[Debug] Running Reference Model Standalone..." << std::endl;

    sim_time = 0;
    while (sim_time < (long long)MAX_SIM_TIME) { // Or a large limit
      difftest_step(false);
      sim_time++;
      if (handle_pending_sigint()) {
        free(p_memory);
        return 130;
      }
      if (ref_cpu.sim_end) {
        cpu.ctx.exit_reason = ExitReason::EBREAK;
        break;
      }
      if (config.progress_interval != 0 &&
          (static_cast<uint64_t>(sim_time) % config.progress_interval) == 0) {
        cout << "[Progress][REF] cycle=" << dec
             << static_cast<uint64_t>(sim_time) << endl;
      }
    }
    std::cout << "[Debug] Ref Model Run Completed." << std::endl;
    free(p_memory);
    return 0;
  }

  // 主循环
  uint64_t next_progress_cycle = config.progress_interval;
  uint64_t last_progress_cycle = 0;
  uint64_t last_progress_commit = 0;
  for (sim_time = 0; sim_time < (long long)MAX_SIM_TIME; sim_time++) {
    if (LOG) {
      cout << "**************************************************************"
           << dec << " cycle: " << sim_time
           << " *************************************************************"
           << endl;
    }

    cpu.cycle();
    maybe_print_o3_progress(config, next_progress_cycle, last_progress_cycle,
                            last_progress_commit);

    if (handle_pending_sigint()) {
      free(p_memory);
      return 130;
    }

    if (config.max_commit_inst != 0 &&
        cpu.ctx.perf.commit_num >= config.max_commit_inst) {
      cpu.ctx.exit_reason = ExitReason::SIMPOINT;
      std::cout << "[sim] Reached committed instruction limit="
                << std::dec << config.max_commit_inst
                << " at cycle=" << cpu.ctx.perf.cycle
                << " ipc_total="
                << ((cpu.ctx.perf.cycle == 0)
                        ? 0.0
                        : static_cast<double>(cpu.ctx.perf.commit_num) /
                              static_cast<double>(cpu.ctx.perf.cycle))
                << std::endl;
    }

    if (cpu.ctx.exit_reason != ExitReason::NONE) {
      break;
    }
  }

  free(p_memory);

  if (sim_time != MAX_SIM_TIME) {
    cout << "\033[38;5;34m-----------------------------\033[0m" << endl;
    cout << "\033[38;5;34mSuccess!!!!\033[0m" << endl;
    cout << "\033[38;5;34m-----------------------------\033[0m" << endl;

    if (cpu.ctx.exit_reason == ExitReason::EBREAK) {
      uint32_t a0 = cpu.get_reg(10);
      return a0;
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
