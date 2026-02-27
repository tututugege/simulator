#pragma once
#include "base_types.h"
#include <cstdio>  // Replaced <stdio.h> with <cstdio>
#include <cstdlib> // For exit()
#include <cstring> // Added for memset

// Helper to compute log2 at compile time
constexpr int clog2(uint64_t n) {
  int res = 0;
  while (n > (1ULL << res))
    res++;
  return res;
}

constexpr bool is_power_of_two_u64(uint64_t n) {
  return n != 0 && ((n & (n - 1)) == 0);
}

// ==========================================
// [System Configuration]
// ==========================================

#ifndef ICACHE_MISS_LATENCY
#define ICACHE_MISS_LATENCY 50
#endif

#ifndef MAX_COMMIT_INST
#define MAX_COMMIT_INST 15000000000
#endif

constexpr uint64_t VIRTUAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024; // 1G elements = 4GB
constexpr uint64_t PHYSICAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024;                      // 1G elements = 4GB
constexpr uint64_t MAX_SIM_TIME = 1000000000000ULL; // 1T cycles (very large)

constexpr int FETCH_WIDTH = 16;
constexpr int DECODE_WIDTH = 8;
static_assert(DECODE_WIDTH > 0, "DECODE_WIDTH must be positive");
static_assert(DECODE_WIDTH <= FETCH_WIDTH,
              "DECODE_WIDTH must be <= FETCH_WIDTH");
constexpr int COMMIT_WIDTH = DECODE_WIDTH;
constexpr int IDU_INST_BUFFER_SIZE = 64;
constexpr int ICACHE_LINE_SIZE = 64; // bytes

constexpr int ARF_NUM = 32;
constexpr int PRF_NUM = 160; // Optimized for 8-wide
constexpr int MAX_BR_NUM = 64;
constexpr int MAX_BR_PER_CYCLE = 4; // Scaled for 8-wide
constexpr int CSR_NUM = 21;

constexpr int ROB_BANK_NUM = 8;
constexpr int ROB_NUM = 128;
constexpr int ROB_LINE_NUM = 16; // (ROB_NUM / ROB_BANK_NUM)

// Sanity Checks moved to later in the file where all parameters are defined

constexpr int WARMUP = 100000000;
constexpr int SIMPOINT_INTERVAL = 100000000;

// ==========================================
// [Debug & Logging Config]
// ==========================================

constexpr uint64_t LOG_START = 0;
constexpr uint64_t BACKEND_LOG_START = 20000000000; // Effectively disabled
constexpr uint64_t MEMORY_LOG_START = LOG_START;
constexpr uint64_t DCACHE_LOG_START = LOG_START;
constexpr uint64_t MMU_LOG_START = LOG_START;

// Master log enable
// #define LOG_ENABLE
// Domain enables (effective only when LOG_ENABLE is enabled)
// #define LOG_MEMORY_ENABLE
// #define LOG_DCACHE_ENABLE
// #define LOG_MMU_ENABLE

extern long long sim_time; // Global simulation time

#ifdef LOG_ENABLE
#define BACKEND_LOG (sim_time >= BACKEND_LOG_START)

#ifdef LOG_MEMORY_ENABLE
#define MEM_LOG (sim_time >= MEMORY_LOG_START)
#else
#define MEM_LOG (0)
#endif

#ifdef LOG_DCACHE_ENABLE
#define DCACHE_LOG (sim_time >= DCACHE_LOG_START)
#else
#define DCACHE_LOG (0)
#endif

#ifdef LOG_MMU_ENABLE
#define MMU_LOG (sim_time >= MMU_LOG_START)
#else
#define MMU_LOG (0)
#endif

// Backward-compatible aliases for existing backend logs
#define LOG (BACKEND_LOG)
#define DEBUG (BACKEND_LOG)
#else
#define BACKEND_LOG (0)
#define LOG (0)
#define DEBUG (0)
#define MEM_LOG (0)
#define DCACHE_LOG (0)
#define MMU_LOG (0)
#endif

constexpr uint32_t DEBUG_ADDR = 0x807a1848; // 0x807a4000

// Feature Flags (Macros used for conditional compilation)
#define CONFIG_DIFFTEST
#define CONFIG_PERF_COUNTER
#define CONFIG_BPU
// MMU domain feature tags (kept enabled for front/back path visibility)
#define CONFIG_DTLB
#define CONFIG_ITLB
// Unified MMU model switch:
// - defined   : I/D side both use TlbMmu (with PTW path)
// - undefined : I/D side both use SimpleMmu (ideal va2pa)
#define CONFIG_TLB_MMU

constexpr uint32_t UART_BASE = 0x10000000;

// ==========================================
// [Instruction & Pipeline Definitions]
// ==========================================

constexpr uint64_t OP_MASK_ALU = (1ULL << UOP_ADD) | (1ULL << UOP_ECALL) |
                                 (1ULL << UOP_EBREAK) | (1ULL << UOP_MRET) |
                                 (1ULL << UOP_SRET) | (1ULL << UOP_SFENCE_VMA) |
                                 (1ULL << UOP_FENCE_I) | (1ULL << UOP_WFI);
constexpr uint64_t OP_MASK_CSR = (1ULL << UOP_CSR);
constexpr uint64_t OP_MASK_MUL = (1ULL << UOP_MUL);
constexpr uint64_t OP_MASK_DIV = (1ULL << UOP_DIV);
constexpr uint64_t OP_MASK_BR = (1ULL << UOP_BR) | (1ULL << UOP_JUMP);
constexpr uint64_t OP_MASK_LD = (1ULL << UOP_LOAD);
constexpr uint64_t OP_MASK_STA = (1ULL << UOP_STA);
constexpr uint64_t OP_MASK_STD = (1ULL << UOP_STD);
constexpr uint64_t OP_MASK_FP = (1ULL << UOP_FP);

// 全局物理端口定义
// 这里的顺序很重要，后面 IQ 会通过下标索引它
// [重要声明] CSR 指令目前硬绑定在 Port 0，如果调整配置，请确保 Port 0 包含
// OP_MASK_CSR
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR |
            OP_MASK_DIV},                        // Port 0: Full ALU + System
    {1, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_FP}, // Port 1: ALU + Mul + FP
    {2, OP_MASK_ALU},                            // Port 2: Simple ALU
    {3, OP_MASK_ALU},                            // Port 3: Simple ALU
    {4, OP_MASK_LD},                             // Port 4: Load 0
    {5, OP_MASK_LD},                             // Port 5: Load 1
    {6, OP_MASK_STA},                            // Port 6: Store Addr
    {7, OP_MASK_STA},                            // Port 6: Store Addr
    {8, OP_MASK_STD},                            // Port 7: Store Data
    {9, OP_MASK_STD},                            // Port 7: Store Data

    {10, OP_MASK_BR}, // Port 8: Branch 0
    {11, OP_MASK_BR}  // Port 9: Branch 1
};

constexpr int ISSUE_WIDTH =
    sizeof(GLOBAL_ISSUE_PORT_CONFIG) / sizeof(GLOBAL_ISSUE_PORT_CONFIG[0]);

// 辅助函数：根据 Port Index 获取 Mask
constexpr uint64_t get_port_capability(int port_idx) {
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    if (cfg.port_idx == port_idx)
      return cfg.support_mask;
  }
  return 0;
}

// 辅助函数：根据 Mask 计算端口数量
constexpr int count_ports_with_mask(uint64_t mask) {
  int count = 0;
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    if (cfg.support_mask & mask)
      count++;
  }
  return count;
}

// 辅助函数：查找支持该 Mask 的第一个端口
constexpr int find_first_port_with_mask(uint64_t mask) {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (GLOBAL_ISSUE_PORT_CONFIG[i].support_mask & mask)
      return i;
  }
  return -1;
}

constexpr int MAX_IQ_DISPATCH_WIDTH = DECODE_WIDTH;
constexpr int MAX_STQ_DISPATCH_WIDTH = 4;
constexpr int MAX_LDQ_DISPATCH_WIDTH = DECODE_WIDTH;
constexpr int MAX_UOPS_PER_INST = 3;

constexpr int ALU_NUM = count_ports_with_mask(OP_MASK_ALU);
constexpr int BRU_NUM = count_ports_with_mask(OP_MASK_BR);
constexpr int STQ_NUM = 64;
constexpr int MAX_INFLIGHT_LOADS = 64;
constexpr int MUL_MAX_LATENCY = 2;
constexpr int DIV_MAX_LATENCY = 18;

// LSU Config
constexpr int LSU_STA_COUNT = count_ports_with_mask(OP_MASK_STA);
constexpr int LSU_LDU_COUNT = count_ports_with_mask(OP_MASK_LD);
constexpr int LSU_AGU_COUNT = LSU_STA_COUNT + LSU_LDU_COUNT;
constexpr int LSU_SDU_COUNT = count_ports_with_mask(OP_MASK_STD);
constexpr int LSU_LOAD_WB_WIDTH = LSU_LDU_COUNT;
constexpr int ITLB_ENTRIES = 32;
constexpr int DTLB_ENTRIES = 32;

constexpr int MAX_WAKEUP_PORTS =
    LSU_LOAD_WB_WIDTH + count_ports_with_mask(OP_MASK_ALU) +
    count_ports_with_mask(OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR);

// Configuration Sanity Checks
static_assert(MAX_BR_NUM <= 64, "MAX_BR_NUM exceeds maximum wire width (64)");
static_assert(STQ_NUM <= 64, "STQ_NUM exceeds maximum wire width (64)");
static_assert(ROB_NUM % ROB_BANK_NUM == 0,
              "ROB_NUM must be a multiple of ROB_BANK_NUM");
static_assert(DECODE_WIDTH == ROB_BANK_NUM,
              "DECODE_WIDTH must be <= ROB_BANK_NUM for ROB row enqueue");
static_assert(PRF_NUM >= ARF_NUM,
              "PRF_NUM must be greater than or equal to ARF_NUM");
static_assert(COMMIT_WIDTH == DECODE_WIDTH,
              "COMMIT_WIDTH must be <= DECODE_WIDTH");
static_assert(MAX_BR_PER_CYCLE > 0, "MAX_BR_PER_CYCLE must be positive");
static_assert(MAX_BR_PER_CYCLE <= MAX_BR_NUM,
              "MAX_BR_PER_CYCLE must be <= MAX_BR_NUM");
static_assert(ICACHE_LINE_SIZE > 0, "ICACHE_LINE_SIZE must be positive");
static_assert((ICACHE_LINE_SIZE % 4) == 0,
              "ICACHE_LINE_SIZE must be word-aligned (multiple of 4 bytes)");
static_assert(is_power_of_two_u64(ICACHE_LINE_SIZE),
              "ICACHE_LINE_SIZE must be a power of two");
static_assert(FETCH_WIDTH * 4 <= ICACHE_LINE_SIZE,
              "FETCH_WIDTH*4 must be >= ICACHE_LINE_SIZE");

// Width Constants for parameterized types
constexpr int AREG_IDX_WIDTH = 6;
constexpr int PRF_IDX_WIDTH = clog2(PRF_NUM);
constexpr int ROB_IDX_WIDTH = clog2(ROB_NUM);
constexpr int STQ_IDX_WIDTH = clog2(STQ_NUM);
constexpr int BR_TAG_WIDTH = clog2(MAX_BR_NUM);
constexpr int BR_MASK_WIDTH = MAX_BR_NUM;
constexpr int CSR_IDX_WIDTH = 12; // Standard RISC-V CSR address width
constexpr int FTQ_SIZE = 64;
static_assert(is_power_of_two_u64(FTQ_SIZE), "FTQ_SIZE must be a power of two");
constexpr int FTQ_IDX_WIDTH = clog2(FTQ_SIZE);
constexpr int FTQ_OFFSET_WIDTH = clog2(FETCH_WIDTH);

// MMIO Address Space
constexpr uint32_t UART_ADDR_BASE = 0x10000000;
constexpr uint32_t UART_ADDR_MASK = 0xFFFFFFF0;
constexpr uint32_t PLIC_ADDR_BASE = 0x0c000000;
constexpr uint32_t PLIC_ADDR_MASK = 0xFC000000;
constexpr uint32_t PLIC_CLAIM_ADDR = 0x0c201004;
constexpr uint32_t CLINT_ADDR_BASE = 0x02000000;
constexpr uint32_t CLINT_ADDR_MASK = 0xFFFF0000;

constexpr int IQ_ALU_PORT_BASE = find_first_port_with_mask(OP_MASK_ALU);
constexpr int IQ_LD_PORT_BASE = find_first_port_with_mask(OP_MASK_LD);
constexpr int IQ_STA_PORT_BASE = find_first_port_with_mask(OP_MASK_STA);
constexpr int IQ_STD_PORT_BASE = find_first_port_with_mask(OP_MASK_STD);
constexpr int IQ_BR_PORT_BASE = find_first_port_with_mask(OP_MASK_BR);

// 计算总功能单元数量 (用于 Bypass 广播)
constexpr int calculate_total_fu_count() {
  int total = 0;
  uint64_t major_masks[] = {OP_MASK_ALU, OP_MASK_CSR, OP_MASK_MUL,
                            OP_MASK_DIV, OP_MASK_BR,  OP_MASK_LD,
                            OP_MASK_STA, OP_MASK_STD, OP_MASK_FP};
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    for (uint64_t m : major_masks) {
      if (cfg.support_mask & m)
        total++;
    }
  }
  return total;
}
constexpr int TOTAL_FU_COUNT = calculate_total_fu_count();

// ==========================================
// 2. IQ 逻辑配置 (IQ Logical Config)
// ==========================================

// Instruction Queue Configuration
constexpr IQStaticConfig GLOBAL_IQ_CONFIG[] = {
    {IQ_INT, 64, DECODE_WIDTH,
     OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, IQ_ALU_PORT_BASE,
     count_ports_with_mask(OP_MASK_ALU)},
    {IQ_LD, 32, DECODE_WIDTH, OP_MASK_LD, IQ_LD_PORT_BASE,
     count_ports_with_mask(OP_MASK_LD)},
    {IQ_STA, 32, DECODE_WIDTH, OP_MASK_STA, IQ_STA_PORT_BASE,
     count_ports_with_mask(OP_MASK_STA)},
    {IQ_STD, 32, DECODE_WIDTH, OP_MASK_STD, IQ_STD_PORT_BASE,
     count_ports_with_mask(OP_MASK_STD)},
    {IQ_BR, 32, DECODE_WIDTH, OP_MASK_BR, IQ_BR_PORT_BASE,
     count_ports_with_mask(OP_MASK_BR)}};

#include "types.h"
