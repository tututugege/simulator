#pragma once

// #include <cassert> // Removed to use custom Assert
#include <cstdlib> // For exit()

// Custom Assert Macro to avoid WSL2 issues
#define Assert(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("\033[1;31mAssertion failed: %s, file %s, line %d\033[0m\n",      \
             #cond, __FILE__, __LINE__);                                       \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#include <cstdio>  // Replaced <stdio.h> with <cstdio>
#include <cstring> // Added for memset
#include <iostream>
#include <string> // Added for std::string
#include <type_traits>

// ==========================================
// [Type Definitions & Utilities]
// ==========================================

// Recursive template to find suitable container type
template <int N>
using AutoType = typename std::conditional<
    N == 1, bool,
    typename std::conditional<
        N <= 8, uint8_t,
        typename std::conditional<
            N <= 16, uint16_t,
            typename std::conditional<
                N <= 32, uint32_t,
                typename std::conditional<N <= 64, uint64_t, uint64_t>::type>::
                type>::type>::type>::type;

template <int N> using wire = AutoType<N>;
template <int N> using reg = AutoType<N>;

typedef wire<7> preg_t;
typedef wire<4> tag_t;

// Standard library usings (kept for compatibility)
using std::cin;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::string;

// ==========================================
// [System Configuration]
// ==========================================

#ifndef ICACHE_MISS_LATENCY
#define ICACHE_MISS_LATENCY 100
#endif

#ifndef MAX_COMMIT_INST
#define MAX_COMMIT_INST 15000000000
#endif

constexpr uint64_t VIRTUAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024; // 1G elements = 4GB
constexpr uint64_t PHYSICAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024;                      // 1G elements = 4GB
constexpr uint64_t MAX_SIM_TIME = 1000000000000ULL; // 1T cycles (very large)

constexpr int FETCH_WIDTH = 4;
constexpr int COMMIT_WIDTH = 4;
constexpr int ISSUE_WIDTH = 8;

constexpr int ARF_NUM = 33;
constexpr int Prf_NUM = 128; // Physical Register File size
constexpr int MAX_BR_NUM = 16;
constexpr int CSR_NUM = 21;

constexpr int ROB_BANK_NUM = 4;
constexpr int ROB_NUM = 128;
constexpr int ROB_LINE_NUM = 32; // (ROB_NUM / ROB_BANK_NUM)

constexpr int WARMUP = 100000000;
constexpr int SIMPOINT_INTERVAL = 100000000;

// ==========================================
// [Debug & Logging Config]
// ==========================================

constexpr uint64_t LOG_START = 0;
#define LOG_ENABLE // Enable logging support (controlled by macros below)

extern long long sim_time; // Global simulation time

#ifndef LOG_ENABLE
#define DEBUG (0)
#define LOG (0 && (sim_time >= (int64_t)LOG_START))
#define MEM_LOG (0 && (sim_time >= (int64_t)LOG_START))
#define DCACHE_LOG (0 && (sim_time >= (int64_t)LOG_START))
#define MMU_LOG (0 && (sim_time >= (int64_t)LOG_START))
#else
                           // Adjust these conditions to enable specific logs
#define DEBUG (0 && (sim_time >= 0))
#define LOG (0 && (sim_time >= (int64_t)LOG_START))
#define MEM_LOG (0 && (sim_time >= (int64_t)LOG_START))
#define DCACHE_LOG (0 && (sim_time >= (int64_t)LOG_START))
#define MMU_LOG (0 && (sim_time >= (int64_t)LOG_START))
#endif

constexpr uint32_t DEBUG_ADDR = 0x807a1848; // 0x807a4000

// Feature Flags (Macros used for conditional compilation)
#define CONFIG_DIFFTEST
#define CONFIG_PERF_COUNTER
// #define CONFIG_MMU
#define CONFIG_BPU
// #define CONFIG_CACHE
// #define ENABLE_MULTI_BR

/*
 * 宽松的va2pa检查：
 * 允许 DUT 判定为 page fault，但是 REF 判定不为
 * page fault 时，通过 DIFFTEST 并以 DUT 为准
 */
#define CONFIG_LOOSE_VA2PA

constexpr uint32_t UART_BASE = 0x10000000;

// ==========================================
// [Instruction & Pipeline Definitions]
// ==========================================

enum UopType {
  UOP_JUMP,
  UOP_ADD,
  UOP_BR,
  UOP_LOAD,
  UOP_STA,
  UOP_STD,
  UOP_CSR,
  UOP_ECALL,
  UOP_EBREAK,
  UOP_SFENCE_VMA,
  UOP_FENCE_I,
  UOP_MRET,
  UOP_SRET,
  UOP_MUL,
  UOP_DIV,
  MAX_UOP_TYPE
};

constexpr uint64_t OP_MASK_ALU = (1ULL << UOP_ADD) | (1ULL << UOP_ECALL) |
                                 (1ULL << UOP_EBREAK) | (1ULL << UOP_MRET) |
                                 (1ULL << UOP_SRET) | (1ULL << UOP_SFENCE_VMA) |
                                 (1ULL << UOP_FENCE_I);
constexpr uint64_t OP_MASK_CSR = (1ULL << UOP_CSR);
constexpr uint64_t OP_MASK_MUL = (1ULL << UOP_MUL);
constexpr uint64_t OP_MASK_DIV = (1ULL << UOP_DIV);
constexpr uint64_t OP_MASK_BR = (1ULL << UOP_BR) | (1ULL << UOP_JUMP);
constexpr uint64_t OP_MASK_LD = (1ULL << UOP_LOAD);
constexpr uint64_t OP_MASK_STA = (1ULL << UOP_STA);
constexpr uint64_t OP_MASK_STD = (1ULL << UOP_STD);

constexpr int MAX_IQ_DISPATCH_WIDTH = 4;
constexpr int MAX_STQ_DISPATCH_WIDTH = 2;
constexpr int MAX_WAKEUP_PORTS = 7;
constexpr int MAX_UOPS_PER_INST = 3;

constexpr int ALU_NUM = 4;
constexpr int BRU_NUM = 1;
constexpr int STQ_NUM = 16;
constexpr int MUL_MAX_LATENCY = 2;
constexpr int DIV_MAX_LATENCY = 18;

// LSU Config
constexpr int LSU_STA_COUNT = 1;
constexpr int LSU_LDU_COUNT = 1;
constexpr int LSU_AGU_COUNT = LSU_STA_COUNT + LSU_LDU_COUNT;
constexpr int LSU_SDU_COUNT = 1;
constexpr int LSU_LOAD_WB_WIDTH = 1;

constexpr int IQ_ALU_PORT_BASE = 0;
constexpr int IQ_LD_PORT_BASE = ALU_NUM;
constexpr int IQ_STA_PORT_BASE = (IQ_LD_PORT_BASE + LSU_LDU_COUNT);
constexpr int IQ_STD_PORT_BASE = (IQ_STA_PORT_BASE + LSU_STA_COUNT);
constexpr int IQ_BR_PORT_BASE = (IQ_STD_PORT_BASE + LSU_SDU_COUNT);

enum IQType {
  IQ_INT,
  IQ_LD,
  IQ_STA,
  IQ_STD,
  IQ_BR,
  IQ_NUM,
};

struct IssuePortConfigInfo {
  int port_idx;          // 物理端口号 (Out.iss2prf 的下标)
  uint64_t support_mask; // 该端口支持的操作掩码 (Capability)
};

constexpr int TOTAL_FU_COUNT = 11;

// ==========================================
// 2. IQ 逻辑配置 (IQ Logical Config)
// ==========================================

struct IQStaticConfig {
  int id;                 // IQ ID
  int size;               // 队列深度
  int dispatch_width;     // 入队宽度 (Dispatch 写端口数)
  uint64_t supported_ops; // IQ 整体接收什么指令 (用于 Dispatch 路由)
  int port_start_idx;
  int port_num;
};

// Instruction Queue Configuration
constexpr IQStaticConfig GLOBAL_IQ_CONFIG[] = {
    {IQ_INT, 32, 4, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, 0,
     4},
    {IQ_LD, 16, 2, OP_MASK_LD, 4, 1},
    {IQ_STA, 16, 2, OP_MASK_STA, 5, 1},
    {IQ_STD, 16, 2, OP_MASK_STD, 6, 1},
    {IQ_BR, 16, 1, OP_MASK_BR, 7, 1}};

// 全局物理端口定义
// 这里的顺序很重要，后面 IQ 会通过下标索引它
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR}, // Port 0
    {1, OP_MASK_ALU | OP_MASK_DIV},               // Port 1
    {2, OP_MASK_ALU},                             // Port 2
    {3, OP_MASK_ALU},                             // Port 3
    {4, OP_MASK_LD},                              // Port 4
    {5, OP_MASK_STA},                             // Port 5
    {6, OP_MASK_STD},                             // Port 6
    {7, OP_MASK_BR}                               // Port 7
};

// 辅助函数：根据 Port Index 获取 Mask
constexpr uint64_t get_port_capability(int port_idx) {
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    if (cfg.port_idx == port_idx)
      return cfg.support_mask;
  }
  return 0;
}

enum InstType {
  NOP,
  JAL,
  JALR,
  ADD,
  BR,
  LOAD,
  STORE,
  CSR,
  ECALL,
  EBREAK,
  SFENCE_VMA,
  FENCE_I,
  MRET,
  SRET,
  MUL,
  DIV,
  AMO
};

// AMO Operations (funct7[6:2])
namespace AmoOp {
constexpr uint8_t ADD = 0b00000;
constexpr uint8_t SWAP = 0b00001;
constexpr uint8_t LR = 0b00010;
constexpr uint8_t SC = 0b00011;
constexpr uint8_t XOR = 0b00100;
constexpr uint8_t OR = 0b01000;
constexpr uint8_t AND = 0b01100;
constexpr uint8_t MIN = 0b10000;
constexpr uint8_t MAX = 0b10100;
constexpr uint8_t MINU = 0b11000;
constexpr uint8_t MAXU = 0b11100;
} // namespace AmoOp

// ==========================================
// [Structs & Classes]
// ==========================================

typedef struct InstUop {
  wire<32> instruction;

  wire<6> dest_areg, src1_areg, src2_areg;
  wire<7> dest_preg, src1_preg, src2_preg; // log2(ROB_NUM)
  wire<7> old_dest_preg;
  wire<32> src1_rdata, src2_rdata;
  wire<32> result;
  wire<32> paddr;

  // 分支预测信息
  wire<1> pred_br_taken;
  wire<1> alt_pred;
  wire<8> altpcpn;
  wire<8> pcpn;
  wire<32> pred_br_pc;
  wire<32> tage_idx[4]; // TN_MAX = 4

  // 分支预测更新信息
  wire<1> mispred;
  wire<1> br_taken;
  wire<32> pc_next;

  wire<1> dest_en, src1_en, src2_en;
  wire<1> src1_busy, src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm; // 好像不用32bit 先用着
  wire<32> pc;  // 未来将会优化pc的获取
  wire<4> tag;
  wire<12> csr_idx;
  wire<7> rob_idx;
  wire<4> stq_idx;
  wire<16> pre_sta_mask;
  wire<16> pre_std_mask;

  // ROB 信息
  wire<2> uop_num;
  wire<2> cplt_num;
  wire<1> rob_flag; // 用于对比指令年龄

  // 异常信息
  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;

  InstType type;
  UopType op;

  // Debug
  bool difftest_skip;
  bool flush_pipe;
  int64_t inst_idx;
  int64_t cplt_time;
  int64_t enqueue_time;

  InstUop() { std::memset(this, 0, sizeof(InstUop)); }
} InstUop;

typedef struct {
  wire<1> valid;
  InstUop uop;
} InstEntry;

typedef struct {
  wire<1> valid;
  wire<7> preg;
} WakeInfo;

#include "PerfCount.h"

// Added to support Remote icache
class SimContext {
public:
  PerfCount perf;
  bool sim_end = false;
  bool is_ckpt;
};
