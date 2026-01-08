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
    RUN,  // 原 Normal模式：从头运行二进制文件 (乱序)
    CKPT, // Ckpt模式：从快照恢复
    FAST  // Fast模式：先单周期快进，再乱序执行
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
  std::cout << "  -m, --mode <run|ckpt|fast>  Set execution mode (default: run)"
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
      {"fast-forward", required_argument, 0, 'f'}, // [新增] 长参数
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // --- A. 解析 Flags ---
  // [修改] getopt 字符串增加 "f:"
  while ((opt = getopt_long(argc, argv, "m:f:h", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'm': {
      std::string m(optarg);
      if (m == "ckpt") {
        config.mode = SimConfig::CKPT;
      } else if (m == "run") {
        config.mode = SimConfig::RUN;
      } else if (m == "fast") { // [新增] 解析 fast 模式
        config.mode = SimConfig::FAST;
      } else {
        std::cerr << "Error: Unknown mode '" << m
                  << "'. Use 'run', 'ckpt', or 'fast'." << std::endl;
        return 1;
      }
      break;
    }
    case 'f': { // [新增] 解析快进数值
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

  // [新增] FAST 模式的逻辑校验
  if (config.mode == SimConfig::FAST) {
    if (config.fast_forward_count == 0) {
      std::cerr << "Error: FAST mode requires a positive number for "
                   "--fast-forward (-f)."
                << std::endl;
      return 1;
    }
  } else {
    // 如果不是 FAST 模式，却指定了 -f，可以报个 Warning
    if (config.fast_forward_count > 0) {
      std::cerr << "Warning: --fast-forward (-f) is ignored in RUN/CKPT mode."
                << std::endl;
    }
  }

  p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];

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
    ref_cpu.fast_run = true;

    for (int i = 0; i < config.fast_forward_count; i++) {
      difftest_step(false);
    }

    cpu.back.restore_from_ref();
    ref_cpu.fast_run = false;
    std::cout << "[Step 2] Run O3 CPU ... " << endl;
  }

  cpu.init();

  // ref_cpu.fast_run = true;
  // while (1) {
  //   difftest_step(false);
  //   sim_time++;
  //   if (sim_time % 100000000 == 0) {
  //     cout << dec << sim_time << endl;
  //   }
  // }

  // main loop
  for (sim_time = 0; sim_time < MAX_SIM_TIME; sim_time++) {
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
    cpu.ctx.perf.perf_print();
    cout << "\033[1;32m-----------------------------\033[0m" << endl;

  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cpu.ctx.perf.perf_print();
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

  delete p_memory;
  return 0;
}
