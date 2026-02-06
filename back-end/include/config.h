#pragma once
#include "types.h"
#include <cstdio>  // Replaced <stdio.h> with <cstdio>
#include <cstdlib> // For exit()
#include <cstring> // Added for memset

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

constexpr int ARF_NUM = 32;
constexpr int PRF_NUM = 128; // Physical Register File size
constexpr int MAX_BR_NUM = 16;
constexpr int MAX_BR_PER_CYCLE = 2;
constexpr int CSR_NUM = 21;

constexpr int ROB_BANK_NUM = 4;
constexpr int ROB_NUM = 128;
constexpr int ROB_LINE_NUM = 32; // (ROB_NUM / ROB_BANK_NUM)

constexpr int WARMUP = 100000000;
constexpr int SIMPOINT_INTERVAL = 100000000;

// ==========================================
// [Debug & Logging Config]
// ==========================================

constexpr uint64_t LOG_START = 185790000;
// #define LOG_ENABLE // Enable logging support (controlled by macros below)

extern long long sim_time; // Global simulation time

#ifdef LOG_ENABLE
                           // Adjust these conditions to enable specific logs
#define LOG (sim_time >= 185790000LL)
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

constexpr int TOTAL_FU_COUNT = 11;

// ==========================================
// 2. IQ 逻辑配置 (IQ Logical Config)
// ==========================================

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
