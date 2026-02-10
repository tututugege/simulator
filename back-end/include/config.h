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

constexpr int FETCH_WIDTH = 8;
constexpr int COMMIT_WIDTH = FETCH_WIDTH;

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
// #define LOG_ENABLE // Enable logging support (controlled by macros below)

extern long long sim_time; // Global simulation time

#ifdef LOG_ENABLE
                           // Adjust these conditions to enable specific logs
#define LOG (sim_time >= (long long)LOG_START)
#define DEBUG (LOG)
#define MEM_LOG (LOG)
#define DCACHE_LOG (LOG)
#define MMU_LOG (LOG)
#else
#define DEBUG (0)
#define LOG (0)
#define MEM_LOG (0)
#define DCACHE_LOG (0)
#define MMU_LOG (0)
#endif

constexpr uint32_t DEBUG_ADDR = 0x807a1848; // 0x807a4000

// Feature Flags (Macros used for conditional compilation)
#define CONFIG_DIFFTEST
#define CONFIG_PERF_COUNTER
#define CONFIG_BPU
// #define CONFIG_MMU
// #define CONFIG_CACHE

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

// 全局物理端口定义
// 这里的顺序很重要，后面 IQ 会通过下标索引它
// [重要声明] CSR 指令目前硬绑定在 Port 0，如果调整配置，请确保 Port 0 包含
// OP_MASK_CSR
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR |
            OP_MASK_DIV},           // Port 0: Full ALU + System
    {1, OP_MASK_ALU | OP_MASK_MUL}, // Port 1: ALU + Mul
    {2, OP_MASK_ALU},               // Port 2: Simple ALU
    {3, OP_MASK_ALU},               // Port 3: Simple ALU
    {4, OP_MASK_LD},                // Port 4: Load 0
    {5, OP_MASK_LD},                // Port 5: Load 1
    {6, OP_MASK_STA},               // Port 6: Store Addr
    {7, OP_MASK_STD},               // Port 7: Store Data
    {8, OP_MASK_BR},                // Port 8: Branch 0
    {9, OP_MASK_BR}                 // Port 9: Branch 1
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

constexpr int MAX_IQ_DISPATCH_WIDTH = FETCH_WIDTH;
constexpr int MAX_STQ_DISPATCH_WIDTH = 4;
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

constexpr int MAX_WAKEUP_PORTS =
    LSU_LOAD_WB_WIDTH + count_ports_with_mask(OP_MASK_ALU) +
    count_ports_with_mask(OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR);

// Configuration Sanity Checks
static_assert(MAX_BR_NUM <= 64, "MAX_BR_NUM exceeds maximum wire width (64)");
static_assert(STQ_NUM <= 64, "STQ_NUM exceeds maximum wire width (64)");
static_assert(ROB_NUM % ROB_BANK_NUM == 0,
              "ROB_NUM must be a multiple of ROB_BANK_NUM");
static_assert(PRF_NUM >= ARF_NUM,
              "PRF_NUM must be greater than or equal to ARF_NUM");
static_assert(MAX_INFLIGHT_LOADS <= STQ_NUM,
              "MAX_INFLIGHT_LOADS should not exceed STQ_NUM");

// Width Constants for parameterized types
constexpr int AREG_IDX_WIDTH = 6;
constexpr int PRF_IDX_WIDTH = clog2(PRF_NUM);
constexpr int ROB_IDX_WIDTH = clog2(ROB_NUM);
constexpr int STQ_IDX_WIDTH = clog2(STQ_NUM);
constexpr int BR_TAG_WIDTH = clog2(MAX_BR_NUM);
constexpr int BR_MASK_WIDTH = MAX_BR_NUM;
constexpr int CSR_IDX_WIDTH = 12; // Standard RISC-V CSR address width

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
  uint64_t major_masks[] = {OP_MASK_ALU, OP_MASK_CSR, OP_MASK_MUL, OP_MASK_DIV,
                            OP_MASK_BR,  OP_MASK_LD,  OP_MASK_STA, OP_MASK_STD};
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
    {IQ_INT, 64, FETCH_WIDTH,
     OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, IQ_ALU_PORT_BASE,
     count_ports_with_mask(OP_MASK_ALU)},
    {IQ_LD, 32, FETCH_WIDTH, OP_MASK_LD, IQ_LD_PORT_BASE,
     count_ports_with_mask(OP_MASK_LD)},
    {IQ_STA, 32, FETCH_WIDTH, OP_MASK_STA, IQ_STA_PORT_BASE,
     count_ports_with_mask(OP_MASK_STA)},
    {IQ_STD, 32, FETCH_WIDTH, OP_MASK_STD, IQ_STD_PORT_BASE,
     count_ports_with_mask(OP_MASK_STD)},
    {IQ_BR, 32, FETCH_WIDTH, OP_MASK_BR, IQ_BR_PORT_BASE,
     count_ports_with_mask(OP_MASK_BR)}};

#include "types.h"
