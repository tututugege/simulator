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

// ============================================================
// [1] Build Control / Debug / Feature Switches
// ============================================================

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

// Diagnostic switch:
// When enabled, clear backend internal stage IO structs at the beginning of
// BackTop::comb() before any comb_* runs. This helps detect hidden dependence
// on previous-cycle IO values (latch-like behavior in combinational paths).
// Keep disabled for normal runs.
#ifndef CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN
#define CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN 1
#endif

// ============================================================
// [2] Global Limits
// ============================================================

constexpr uint64_t VIRTUAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024; // 1G elements = 4GB
constexpr uint64_t PHYSICAL_MEMORY_LENGTH =
    1ULL * 1024 * 1024 * 1024;                      // 1G elements = 4GB
constexpr uint64_t MAX_SIM_TIME = 1000000000000ULL; // 1T cycles (very large)

// ============================================================
// [2] Frontend / Backend Pipeline Width
// ============================================================
constexpr int FETCH_WIDTH = 16;
constexpr int DECODE_WIDTH = 8;
static_assert(FETCH_WIDTH > 0, "FETCH_WIDTH must be positive");
static_assert(DECODE_WIDTH > 0, "DECODE_WIDTH must be positive");
static_assert(DECODE_WIDTH <= FETCH_WIDTH,
              "DECODE_WIDTH must be <= FETCH_WIDTH");
constexpr int COMMIT_WIDTH = DECODE_WIDTH;
constexpr int IDU_INST_BUFFER_SIZE = 64;

// ============================================================
// [2.1] Frontend/Backend Shared Branch-Predictor Metadata Sizes
// ============================================================
// Metadata plumbing sizes for predict -> commit roundtrip.
constexpr int BPU_SCL_META_NTABLE = 8;
constexpr int BPU_SCL_META_IDX_BITS = 16;
constexpr int BPU_LOOP_META_IDX_BITS = 16;
constexpr int BPU_LOOP_META_TAG_BITS = 16;

// ============================================================
// [3] I-Cache Config
// ============================================================
constexpr int ICACHE_LINE_SIZE = 64; // bytes
constexpr int ICACHE_MISS_LATENCY = 50;

// Enable the dedicated AXI-backed icache memory path.
// Keep this disabled when axi-interconnect-kit is not present.
#ifndef CONFIG_ICACHE_USE_AXI_MEM_PORT
#define CONFIG_ICACHE_USE_AXI_MEM_PORT 0
#endif

// AXI protocol flavor for the dedicated icache memory path.
// 4 = AXI4 path, 3 = AXI3 path.
#ifndef CONFIG_AXI_PROTOCOL
#define CONFIG_AXI_PROTOCOL 4
#endif

constexpr int ICACHE_WAY_NUM = 8;
constexpr int ICACHE_OFFSET_BITS = clog2(ICACHE_LINE_SIZE);
constexpr int ICACHE_INDEX_BITS = 12 - ICACHE_OFFSET_BITS;
constexpr int ICACHE_SET_NUM = 1 << ICACHE_INDEX_BITS;
constexpr int ICACHE_WORD_NUM = ICACHE_LINE_SIZE / 4;
constexpr int ICACHE_TAG_BITS = 32 - ICACHE_INDEX_BITS - ICACHE_OFFSET_BITS;
constexpr uint32_t ICACHE_TAG_MASK = (1u << ICACHE_TAG_BITS) - 1u;

// ============================================================
// [4] D-Cache (SimpleCache) Config
// ============================================================
constexpr int DCACHE_LINE_SIZE = ICACHE_LINE_SIZE; // bytes
constexpr int DCACHE_HIT_LATENCY = 1;
constexpr int DCACHE_L2_HIT_LATENCY = 8;
constexpr int DCACHE_MEM_LATENCY = 50;
// Backward-compatible alias; prefer DCACHE_MEM_LATENCY for new code.
constexpr int DCACHE_MISS_LATENCY = DCACHE_MEM_LATENCY;
constexpr int DCACHE_WAY_NUM = 4;
constexpr int DCACHE_OFFSET_BITS = clog2(DCACHE_LINE_SIZE);
constexpr int DCACHE_INDEX_BITS = 8;
constexpr int DCACHE_SET_NUM = 1 << DCACHE_INDEX_BITS;
constexpr int DCACHE_WORD_NUM = DCACHE_LINE_SIZE / 4;
constexpr int DCACHE_TAG_BITS = 32 - DCACHE_INDEX_BITS - DCACHE_OFFSET_BITS;
constexpr uint32_t DCACHE_TAG_MASK = (1u << DCACHE_TAG_BITS) - 1u;
constexpr int DCACHE_MAX_PENDING_REQS = 256;
constexpr bool DCACHE_L2_ENABLE = false;
constexpr int DCACHE_L2_LINE_SIZE = DCACHE_LINE_SIZE; // bytes
constexpr int DCACHE_L2_WAY_NUM = 8;
constexpr int DCACHE_L2_OFFSET_BITS = clog2(DCACHE_L2_LINE_SIZE);
constexpr int DCACHE_L2_INDEX_BITS = 11;
constexpr int DCACHE_L2_SET_NUM = 1 << DCACHE_L2_INDEX_BITS;
constexpr int DCACHE_L2_WORD_NUM = DCACHE_L2_LINE_SIZE / 4;
constexpr int DCACHE_L2_TAG_BITS =
    32 - DCACHE_L2_INDEX_BITS - DCACHE_L2_OFFSET_BITS;
constexpr uint32_t DCACHE_L2_TAG_MASK = (1u << DCACHE_L2_TAG_BITS) - 1u;

// ============================================================
// [5] Core Resource Size
// ============================================================
constexpr int ARF_NUM = 32;
constexpr int PRF_NUM = 160; // Tuned for 4-wide
constexpr int MAX_BR_NUM = 64;
// Branch tag allocation bandwidth per cycle.
// Keep it aligned with decode width on wide frontend/backend configurations.
constexpr int MAX_BR_PER_CYCLE = DECODE_WIDTH;
constexpr int CSR_NUM = 21;

constexpr int ROB_BANK_NUM = DECODE_WIDTH;
constexpr int ROB_NUM = 128;
constexpr int ROB_LINE_NUM = ROB_NUM / ROB_BANK_NUM; // (ROB_NUM / ROB_BANK_NUM)

// ============================================================
// [6] SimPoint Config
// ============================================================
constexpr int WARMUP = 100000000;
constexpr int SIMPOINT_INTERVAL = 100000000;

// ============================================================
// [7] FTQ Config
// ============================================================
constexpr int FTQ_SIZE = 64;
static_assert(is_power_of_two_u64(FTQ_SIZE), "FTQ_SIZE must be a power of two");

// ============================================================
// [9] Uop Capability Masks
// ============================================================

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

// ============================================================
// [10] Issue Port Layout
// ============================================================
// Auto-generate sequential port_idx in GLOBAL_ISSUE_PORT_CONFIG.
// Use a fixed base captured once; avoid macro aliasing __COUNTER__ directly.
enum { ISSUE_PORT_COUNTER_BASE = __COUNTER__ };
#define PORT_CFG(mask) {(__COUNTER__ - ISSUE_PORT_COUNTER_BASE - 1), (mask)}

// 全局物理端口定义
// 这里的顺序很重要, 后面 IQ 会通过下标索引它
// 属于同类的请挨在一起，IQ会通过base+offset来绑定port
// CSR 指令目前硬绑定在 Port 0，如果调整配置，请确保 Port 0 包含 OP_MASK_CSR
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    PORT_CFG(OP_MASK_ALU | OP_MASK_MUL |
             OP_MASK_CSR),               // Port 0: ALU + MUL/DIV + CSR
    PORT_CFG(OP_MASK_ALU | OP_MASK_DIV), // Port 1: Simple ALU
    PORT_CFG(OP_MASK_ALU),               // Port 1: Simple ALU
    PORT_CFG(OP_MASK_ALU),               // Port 1: Simple ALU
    PORT_CFG(OP_MASK_LD),                // Port 3: Load 1
    PORT_CFG(OP_MASK_LD),                // Port 3: Load 1
    PORT_CFG(OP_MASK_STA),               // Port 4: Store Addr
    PORT_CFG(OP_MASK_STA),               // Port 4: Store Addr
    PORT_CFG(OP_MASK_STD),               // Port 5: Store Data
    PORT_CFG(OP_MASK_STD),               // Port 5: Store Data
    PORT_CFG(OP_MASK_BR),                // Port 6: Branch 0
    PORT_CFG(OP_MASK_BR)                 // Port 7: Branch 1
};
#undef PORT_CFG

constexpr int ISSUE_WIDTH =
    sizeof(GLOBAL_ISSUE_PORT_CONFIG) / sizeof(GLOBAL_ISSUE_PORT_CONFIG[0]);

// Helper: query capability by physical port index.
constexpr uint64_t get_port_capability(int port_idx) {
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    if (cfg.port_idx == port_idx)
      return cfg.support_mask;
  }
  return 0;
}

// Helper: count ports supporting a given op mask.
constexpr int count_ports_with_mask(uint64_t mask) {
  int count = 0;
  for (const auto &cfg : GLOBAL_ISSUE_PORT_CONFIG) {
    if (cfg.support_mask & mask)
      count++;
  }
  return count;
}

// Helper: find first port supporting a given op mask.
constexpr int find_first_port_with_mask(uint64_t mask) {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (GLOBAL_ISSUE_PORT_CONFIG[i].support_mask & mask)
      return i;
  }
  return -1;
}

// ============================================================
// [11] Dispatch / Issue / Queue Width
// ============================================================
constexpr int MAX_IQ_DISPATCH_WIDTH = DECODE_WIDTH;
constexpr int MAX_STQ_DISPATCH_WIDTH = DECODE_WIDTH;
constexpr int MAX_LDQ_DISPATCH_WIDTH = DECODE_WIDTH;
constexpr int MAX_UOPS_PER_INST = 3;

// ============================================================
// [12] Backend Functional Unit / Queue Capacity
// ============================================================
constexpr int ALU_NUM = count_ports_with_mask(OP_MASK_ALU);
constexpr int BRU_NUM = count_ports_with_mask(OP_MASK_BR);
constexpr int STQ_SIZE = 64;
constexpr int LDQ_SIZE = 64;
constexpr int MUL_MAX_LATENCY = 2;
constexpr int DIV_MAX_LATENCY = 18;

// LSU / TLB config derived from issue port layout.
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

// ============================================================
// [12.1] ISU scheduling policy
// ============================================================
enum class IssueSchedulePolicy : int {
  IQ_SLOT_PRIORITY = 0, // Deterministic slot-index priority
  ROB_OLDEST_FIRST = 1, // Compare by (rob_flag, rob_idx)
};

// Default policy.
constexpr IssueSchedulePolicy ISSUE_SCHEDULE_POLICY =
    IssueSchedulePolicy::IQ_SLOT_PRIORITY;

// ============================================================
// [13] ISU / IQ / EXU / FU Derived Port Bases
// ============================================================
constexpr int IQ_ALU_PORT_BASE = find_first_port_with_mask(OP_MASK_ALU);
constexpr int IQ_LD_PORT_BASE = find_first_port_with_mask(OP_MASK_LD);
constexpr int IQ_STA_PORT_BASE = find_first_port_with_mask(OP_MASK_STA);
constexpr int IQ_STD_PORT_BASE = find_first_port_with_mask(OP_MASK_STD);
constexpr int IQ_BR_PORT_BASE = find_first_port_with_mask(OP_MASK_BR);

// ============================================================
// [14] ISU / IQ / EXU / FU Derived Counts
// ============================================================
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

// ============================================================
// [15] IQ Static Config
// ============================================================
// Instruction queue layout per queue type.
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

// ============================================================
// [16] Global Sanity Checks
// ============================================================
// Configuration Sanity Checks
static_assert(MAX_BR_NUM <= 64, "MAX_BR_NUM exceeds maximum wire width (64)");
static_assert(STQ_SIZE > 0, "STQ_SIZE must be positive");
static_assert(LDQ_SIZE > 0, "LDQ_SIZE must be positive");
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
              "FETCH_WIDTH*4 must be <= ICACHE_LINE_SIZE");
static_assert(ICACHE_WAY_NUM > 0, "ICACHE_WAY_NUM must be positive");
static_assert(ICACHE_OFFSET_BITS > 0, "ICACHE_OFFSET_BITS must be positive");
static_assert(ICACHE_INDEX_BITS > 0, "ICACHE_INDEX_BITS must be positive");
static_assert(ICACHE_WORD_NUM == ICACHE_LINE_SIZE / 4,
              "ICACHE_WORD_NUM must match ICACHE_LINE_SIZE / 4");
static_assert(ICACHE_TAG_BITS > 0, "ICACHE_TAG_BITS must be positive");
static_assert(ICACHE_SET_NUM > 0, "ICACHE_SET_NUM must be positive");
static_assert(ICACHE_TAG_MASK != 0, "ICACHE_TAG_MASK must be non-zero");
static_assert(DCACHE_LINE_SIZE > 0, "DCACHE_LINE_SIZE must be positive");
static_assert((DCACHE_LINE_SIZE % 4) == 0,
              "DCACHE_LINE_SIZE must be word-aligned (multiple of 4 bytes)");
static_assert(is_power_of_two_u64(DCACHE_LINE_SIZE),
              "DCACHE_LINE_SIZE must be a power of two");
static_assert(DCACHE_L2_LINE_SIZE > 0, "DCACHE_L2_LINE_SIZE must be positive");
static_assert((DCACHE_L2_LINE_SIZE % 4) == 0,
              "DCACHE_L2_LINE_SIZE must be word-aligned (multiple of 4 bytes)");
static_assert(is_power_of_two_u64(DCACHE_L2_LINE_SIZE),
              "DCACHE_L2_LINE_SIZE must be a power of two");
static_assert(DCACHE_WAY_NUM > 0, "DCACHE_WAY_NUM must be positive");
static_assert(DCACHE_OFFSET_BITS > 0, "DCACHE_OFFSET_BITS must be positive");
static_assert(DCACHE_INDEX_BITS > 0, "DCACHE_INDEX_BITS must be positive");
static_assert(DCACHE_WORD_NUM == DCACHE_LINE_SIZE / 4,
              "DCACHE_WORD_NUM must match DCACHE_LINE_SIZE / 4");
static_assert(DCACHE_TAG_BITS > 0, "DCACHE_TAG_BITS must be positive");
static_assert(DCACHE_SET_NUM > 0, "DCACHE_SET_NUM must be positive");
static_assert(DCACHE_TAG_MASK != 0, "DCACHE_TAG_MASK must be non-zero");
static_assert(DCACHE_L2_WAY_NUM > 0, "DCACHE_L2_WAY_NUM must be positive");
static_assert(DCACHE_L2_OFFSET_BITS > 0,
              "DCACHE_L2_OFFSET_BITS must be positive");
static_assert(DCACHE_L2_INDEX_BITS > 0,
              "DCACHE_L2_INDEX_BITS must be positive");
static_assert(DCACHE_L2_WORD_NUM == DCACHE_L2_LINE_SIZE / 4,
              "DCACHE_L2_WORD_NUM must match DCACHE_L2_LINE_SIZE / 4");
static_assert(DCACHE_L2_TAG_BITS > 0, "DCACHE_L2_TAG_BITS must be positive");
static_assert(DCACHE_L2_SET_NUM > 0, "DCACHE_L2_SET_NUM must be positive");
static_assert(DCACHE_L2_TAG_MASK != 0, "DCACHE_L2_TAG_MASK must be non-zero");
static_assert(LSU_LDU_COUNT <= LSU_AGU_COUNT,
              "LSU_LDU_COUNT must be <= LSU_AGU_COUNT");
static_assert(LSU_STA_COUNT <= LSU_AGU_COUNT,
              "LSU_STA_COUNT must be <= LSU_AGU_COUNT");
static_assert(ALU_NUM > 0, "ALU_NUM must be positive");
static_assert(BRU_NUM > 0, "BRU_NUM must be positive");
static_assert(IQ_ALU_PORT_BASE >= 0, "IQ_ALU_PORT_BASE not found");
static_assert(IQ_LD_PORT_BASE >= 0, "IQ_LD_PORT_BASE not found");
static_assert(IQ_STA_PORT_BASE >= 0, "IQ_STA_PORT_BASE not found");
static_assert(IQ_STD_PORT_BASE >= 0, "IQ_STD_PORT_BASE not found");
static_assert(IQ_BR_PORT_BASE >= 0, "IQ_BR_PORT_BASE not found");
static_assert(DTLB_ENTRIES > 0, "DTLB_ENTRIES must be positive");
static_assert(ITLB_ENTRIES > 0, "ITLB_ENTRIES must be positive");

// ============================================================
// [17] Bit Width Definitions
// ============================================================
// Width constants for parameterized wire/register types.
constexpr int AREG_IDX_WIDTH = 6;
constexpr int PRF_IDX_WIDTH = clog2(PRF_NUM);
constexpr int ROB_IDX_WIDTH = clog2(ROB_NUM);
constexpr int STQ_IDX_WIDTH = clog2(STQ_SIZE);
constexpr int LDQ_IDX_WIDTH = clog2(LDQ_SIZE);
constexpr int BR_TAG_WIDTH = clog2(MAX_BR_NUM);
constexpr int BR_MASK_WIDTH = MAX_BR_NUM;
constexpr int CSR_IDX_WIDTH = 12; // Standard RISC-V CSR address width
constexpr int FTQ_IDX_WIDTH = clog2(FTQ_SIZE);
constexpr int FTQ_OFFSET_WIDTH = clog2(FETCH_WIDTH);

// ============================================================
// [18] MMIO Address Space
// ============================================================
constexpr uint32_t UART_ADDR_BASE = 0x10000000;
constexpr uint32_t UART_ADDR_MASK = 0xFFFFFFF0;
constexpr uint32_t PLIC_ADDR_BASE = 0x0c000000;
constexpr uint32_t PLIC_ADDR_MASK = 0xFC000000;
constexpr uint32_t PLIC_CLAIM_ADDR = 0x0c201004;
constexpr uint32_t CLINT_ADDR_BASE = 0x02000000;
constexpr uint32_t CLINT_ADDR_MASK = 0xFFFF0000;

#include "types.h"
