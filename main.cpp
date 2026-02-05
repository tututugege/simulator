#include "diff.h"
#include <SimCpu.h>
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <getopt.h>

namespace fs = std::filesystem;
using namespace std;
extern RefCpu ref_cpu;

// 1. 定义配置结构
struct SimConfig {
  // [修改] 增加 FAST 模式
  enum Mode {
    RUN,     // 运行模式：从头运行二进制文件 (乱序)
    CKPT,    // 快照模式：从快照恢复
    FAST,    // 快速模式：先单周期快进，再乱序执行
    REF_ONLY // 仅运行 Reference Model
  } mode = RUN;

  // 统一的目标文件路径
  std::string target_file;

  // [新增] 存储 Fast-forward 的指令数/周期数
  uint64_t fast_forward_count = 0;
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

// 3. 文件检查函数
bool validate_path(const std::string &path) {
  if (path.empty()) {
    std::cerr << "Error: Target file path is empty." << std::endl;
    return false;
  }
  if (!fs::exists(path)) {
    std::cerr << "Error: File not found: " << path << std::endl;
    return false;
  }
  if (fs::is_directory(path)) {
    std::cerr << "Error: Path is a directory: " << path << std::endl;
    return false;
  }
  return true;
}

long long sim_time = 0;
SimCpu cpu;

int main(int argc, char *argv[]) {

  SimConfig config;

  // 长参数定义
  static struct option long_options[] = {
      {"mode", required_argument, 0, 'm'},
      {"fast-forward", required_argument, 0, 'f'}, // 快进参数
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // --- A. 解析命令行参数 ---
  while ((opt = getopt_long(argc, argv, "m:f:h", long_options,
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

  // --- C. 校验 ---
  if (!validate_path(config.target_file)) {
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



  p_memory = (uint32_t *)calloc(PHYSICAL_MEMORY_LENGTH, sizeof(uint32_t));
  cpu.init();

  // --- D. 模拟器启动逻辑 ---
  if (config.mode == SimConfig::RUN) {
    // [修改] 日志更新为 Binary Image
    std::cout << "[Mode] RUN: Loading Binary Image..." << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.load_image(config.target_file);
  } else if (config.mode == SimConfig::CKPT) {
    std::cout << "[Mode] CKPT: Restoring from Snapshot..." << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.restore_checkpoint(config.target_file);
    cpu.ctx.is_ckpt = true;
  } else if (config.mode == SimConfig::FAST) {
    std::cout << "[Mode] FAST: Hybrid Execution Strategy" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    std::cout << "[Step 1] Fast-forwarding (Simple CPU) for "
              << config.fast_forward_count << " cycles..." << std::endl;

    cpu.back.load_image(config.target_file);
    ref_cpu.strict_mmu_check = false;
    ref_cpu.uart_print = true;

    for (uint64_t i = 0; i < config.fast_forward_count; i++) {
      difftest_step(false);
    }

    cpu.back.restore_from_ref();
    cpu.restore_pc(cpu.back.number_PC); // 强制同步前端 PC
    ref_cpu.strict_mmu_check = true;
    ref_cpu.uart_print = false;

    std::cout << "[Step 2] Run O3 CPU ... " << endl;
  } else if (config.mode == SimConfig::REF_ONLY) {
    std::cout << "[Mode] REF_ONLY: Reference Model Validation" << std::endl;
    std::cout << "[File] " << config.target_file << std::endl;
    cpu.back.load_image(config.target_file);
    ref_cpu.uart_print = true;

    // cpu.init(); // Moved to top

    ref_cpu.strict_mmu_check = false;

    std::cout << "[Debug] Running Reference Model Standalone..." << std::endl;

    sim_time = 0;
    while (sim_time < (long long)MAX_SIM_TIME) { // Or a large limit
      difftest_step(false);
      sim_time++;
      if (sim_time % 10000000 == 0) {
        cout << dec << sim_time << endl;
      }
      // Since Ref doesn't have an explicit 'end' signal exposed easily, usually
      // relies on cycles or trap
    }
    std::cout << "[Debug] Ref Model Run Completed." << std::endl;
    free(p_memory);
    return 0;
  }

  // cpu.init(); // Moved to top

  // 主循环
  for (sim_time = 0; sim_time < (long long)MAX_SIM_TIME; sim_time++) {
    if (sim_time % 10000000 == 0) {
      cout << dec << sim_time << endl;
    }
    if (LOG) {
      cout << "**************************************************************"
           << dec << " cycle: " << sim_time
           << " *************************************************************"
           << endl;
    }

    cpu.cycle();

    if (cpu.ctx.sim_end)
      break;

    if (cpu.ctx.perf.commit_num >= MAX_COMMIT_INST)
      break;
  }

  if (sim_time != MAX_SIM_TIME) {
    cout << "\033[1;32m-----------------------------\033[0m" << endl;
    cout << "\033[1;32mSuccess!!!!\033[0m" << endl;
    // cpu.ctx.perf.perf_print();  // Disabled for cleaner output
    cout << "\033[1;32m-----------------------------\033[0m" << endl;

  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cpu.ctx.perf.perf_print();
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

  free(p_memory);
  return 0;
}
