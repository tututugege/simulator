/*
 * Architecture of current I-Cache design:
 * - 8-way set associative cache
 * - 128 sets
 * - 32 bytes per cache line
 * - Random replacement policy
 * - 2-stage pipeline: comb1 -> seq1 -> comb2
 *
 * Address split:
 * PC[31:12], PC[11:5], PC[4:0]
 * - PC[31:12]: 20-bit Tag
 * - PC[11:5]: 7-bit Index to cache set
 * - PC[4:0]: 5-bit Byte offset within a cache line
 *    - PC[4:2]: 3-bit Word offset within a cache line
 *    - PC[1:0]: 2-bit Byte offset within a word
 *
 * 两级流水线
 * - 第一级流水线(comb1)：取到 IFU 请求 PC 对应 Index 的 cache set
 * - 第二级流水线(comb2)：在该 cache set 中查找 Tag，决定是否命中
 */
#ifndef ICACHE_MODULE_H
#define ICACHE_MODULE_H

#include <cstdint>
#include <frontend.h>
#include <iostream>

// -----------------------------------------------------------------------------
// SRAM lookup latency model (ICache)
// -----------------------------------------------------------------------------
// Legacy compatibility macro:
// - 0: register-style lookup
// - 1: SRAM-style lookup
// New code should prefer ICACHE_LOOKUP_LATENCY (0 = register, 1..N = SRAM).
#ifndef ICACHE_USE_SRAM_MODEL
#define ICACHE_USE_SRAM_MODEL 0
#endif
// Fixed latency (cycles). Latency=1 means data available next cycle.
// Latency=0 is treated as 1 when SRAM model is enabled.
#ifndef ICACHE_SRAM_FIXED_LATENCY
#define ICACHE_SRAM_FIXED_LATENCY 1
#endif
// Random latency enable (1=random, 0=fixed)
#ifndef ICACHE_SRAM_RANDOM_DELAY
#define ICACHE_SRAM_RANDOM_DELAY 0
#endif
// Random latency range (inclusive). Values <1 are clamped to 1.
#ifndef ICACHE_SRAM_RANDOM_MIN
#define ICACHE_SRAM_RANDOM_MIN 1
#endif
#ifndef ICACHE_SRAM_RANDOM_MAX
#define ICACHE_SRAM_RANDOM_MAX 4
#endif
// Unified lookup latency interface:
// - 0: register-style lookup (same-cycle set read path)
// - 1..N: SRAM-style lookup with N-cycle response latency model
// Default keeps backward compatibility with ICACHE_USE_SRAM_MODEL.
#ifndef ICACHE_LOOKUP_LATENCY
#if ICACHE_USE_SRAM_MODEL
#define ICACHE_LOOKUP_LATENCY ICACHE_SRAM_FIXED_LATENCY
#else
#define ICACHE_LOOKUP_LATENCY 0
#endif
#endif

// -----------------------------------------------------------------------------
// ICache V1 (blocking) configurable knobs
// -----------------------------------------------------------------------------
#ifndef ICACHE_V1_WAYS
#define ICACHE_V1_WAYS 8
#endif
// Lookup set-view data source (V1):
// - 0: lookup reads from the internal table state.
// - 1: lookup reads from io.lookup_in (external-fed set view).
// Note: kept as a build-time knob so it is not part of generalized PI/PO.
#ifndef ICACHE_LOOKUP_FROM_INPUT
#define ICACHE_LOOKUP_FROM_INPUT 0
#endif

namespace icache_module_n {
// -----------------------------------------------------------------------------
// ICache V1 derived parameters (for generalized-IO structs)
// -----------------------------------------------------------------------------
static constexpr uint32_t ICACHE_V1_OFFSET_BITS = __builtin_ctz(ICACHE_LINE_SIZE);
static constexpr uint32_t ICACHE_V1_INDEX_BITS = 12 - ICACHE_V1_OFFSET_BITS;
static constexpr uint32_t ICACHE_V1_SET_NUM = 1u << ICACHE_V1_INDEX_BITS;
static constexpr uint32_t ICACHE_V1_WORD_NUM = 1u << (ICACHE_V1_OFFSET_BITS - 2);
static constexpr uint32_t ICACHE_V1_TAG_BITS = 20;

// i-Cache State
enum ICacheState {
  IDLE,         // Idle state
  SWAP_IN,      // Swapping in state
  SWAP_IN_OKEY, // Swapping in successful
  DRAIN         // Draining memory response after refetch
};
// AXI Memory Channel State
enum AXIState {
  AXI_IDLE, // Idle state
  AXI_BUSY, // Busy state
};

// -----------------------------------------------------------------------------
// Generalized-IO: lookup inputs (split from external inputs)
// -----------------------------------------------------------------------------
struct ICache_lookup_in_t {
  // Transfer-valid for the lookup set view in this cycle.
  // - register-style lookup: typically true every cycle
  // - SRAM-style lookup: asserted only when the read response is ready
  wire<1> lookup_resp_valid = false;
  // Set view (WAYS x LINE_WORDS).
  wire<32> lookup_set_data[ICACHE_V1_WAYS][ICACHE_V1_WORD_NUM] = {{0}};
  wire<20> lookup_set_tag[ICACHE_V1_WAYS] = {0};
  wire<1> lookup_set_valid[ICACHE_V1_WAYS] = {false};
};

// -----------------------------------------------------------------------------
// Generalized-IO: register state (sequential) excluding perf counters
// -----------------------------------------------------------------------------
struct ICache_regs_t {
  // Pipeline registers (between pipe1 and pipe2)
  reg<1> pipe_valid_r = false;
  reg<32> pipe_cache_set_data_r[ICACHE_V1_WAYS][ICACHE_V1_WORD_NUM] = {{0}};
  reg<20> pipe_cache_set_tag_r[ICACHE_V1_WAYS] = {0};
  reg<1> pipe_cache_set_valid_r[ICACHE_V1_WAYS] = {false};
  reg<32> pipe_pc_r = 0;
  reg<7> pipe_index_r = 0;

  // FSM + memory channel registers
  reg<2> state = static_cast<reg<2>>(IDLE);
  reg<1> mem_axi_state = static_cast<reg<1>>(AXI_IDLE);

  // Memory response registers
  reg<32> mem_resp_data_r[ICACHE_LINE_SIZE / 4] = {0};

  // Replacement / translation state
  reg<8> replace_idx = 0;
  reg<20> ppn_r = 0;

  // Lookup in-flight state (resource state, not SRAM implementation timing).
  reg<1> lookup_pending_r = false;
  reg<7> lookup_index_r = 0;
  reg<32> lookup_pc_r = 0;
};

// Generalized-IO note:
// `reg_write` and `regs` share the same structure by design.
using ICache_reg_write_t = ICache_regs_t;

// -----------------------------------------------------------------------------
// Generalized-IO: table write controls (observable write port)
// -----------------------------------------------------------------------------
struct ICache_table_write_t {
  wire<1> we = false;
  wire<7> index = 0;
  wire<8> way = 0;
  wire<32> data[ICACHE_LINE_SIZE / 4] = {0};
  wire<20> tag = 0;
  wire<1> valid = false;
};

struct ICache_in_t {
  // Input from the IFU (Instruction Fetch Unit)
  wire<32> pc = 0;        // Program Counter
  wire<1> ifu_req_valid = false;  // Fetch enable signal
  wire<1> ifu_resp_ready = true;  // actually always true in current design
  wire<1> refetch = false;        // Refetch signal from Top
  wire<1> flush = false;          // fence.i flush signal from Top

  // Input from MMU (Memory Management Unit)
  wire<20> ppn = 0;    // Physical Page Number
  wire<1> ppn_valid = false;  // PPN valid signal
  wire<1> page_fault = false; // page fault exception signal

  // Input from memory
  wire<1> mem_req_ready = false;
  wire<1> mem_resp_valid = false;
  // For compatibility with ICacheV2 top-level wiring (ignored by V1).
  wire<4> mem_resp_id = 0;
  wire<32> mem_resp_data[ICACHE_LINE_SIZE / 4] = {0}; // Data from memory (Cache line)
};

struct ICache_out_t {
  // Output to the IFU (Instruction Fetch Unit)
  wire<1> miss = false;           // Cache miss signal
  wire<1> ifu_resp_valid = false; // Indicates if output data is valid
  wire<1> ifu_req_ready = false;  // Indicates if i-cache is allow to accept next PC
  wire<32> ifu_resp_pc = 0;    // PC corresponding to ifu_resp
  wire<32> rd_data[ICACHE_LINE_SIZE / 4] = {0}; // Data read from cache
  wire<1> ifu_page_fault = false;                 // page fault exception signal

  // Output to MMU (Memory Management Unit)
  wire<1> ppn_ready = false; // ready to accept PPN
  // MMU request (drive vtag for translation; unify with ICacheV2 top wiring)
  wire<1> mmu_req_valid = false;
  wire<20> mmu_req_vtag = 0;

  // Output to memory
  wire<1> mem_req_valid = false;    // Memory request signal
  wire<32> mem_req_addr = 0;     // Address for memory access
  wire<4> mem_req_id = 0;        // For compatibility with ICacheV2 (always 0)
  wire<1> mem_resp_ready = false;
};

// cache io
struct ICache_IO_t {
  ICache_in_t in;
  ICache_regs_t regs;
  ICache_lookup_in_t lookup_in;
  ICache_out_t out;
  ICache_reg_write_t reg_write;
  ICache_table_write_t table_write;
};

class ICache {
public:
  // Constructor
  ICache();

  void reset();
  void invalidate_all();
  void comb();
  void comb_pipe1();
  void comb_pipe2();
  void seq();
  void seq_pipe1();

  // Debug/verification helper: export the set view that the lookup stage reads
  // in the current cycle for the given pc (including SRAM pending selection).
  void export_lookup_set_for_pc(uint32_t pc,
                                uint32_t out_data[ICACHE_V1_WAYS][ICACHE_LINE_SIZE / 4],
                                uint32_t out_tag[ICACHE_V1_WAYS],
                                bool out_valid[ICACHE_V1_WAYS]) const;

  // IO ports
  ICache_IO_t io;

  // Debug
  void log_state();
  void log_tag(uint32_t index);
  void log_valid(uint32_t index);
  void log_pipeline();
  int valid_line_num() {
    int count = 0;
    for (uint32_t i = 0; i < set_num; ++i) {
      for (uint32_t j = 0; j < way_cnt; ++j) {
        if (cache_valid[i][j]) {
          count++;
        }
      }
    }
    return count;
  }

private:
  /*
   * Cache parameters
   *
   * offset_bits + index_bits + tag_bits = 32
   * for current design, tag_bits = 20
   */
  static uint32_t const offset_bits =
      __builtin_ctz(ICACHE_LINE_SIZE); // log2(ICACHE_LINE_SIZE)
  static uint32_t const index_bits = 12 - offset_bits;
  static uint32_t const set_num = 1 << index_bits; // Total number of cache sets
  static uint32_t const word_num =
      1 << (offset_bits - 2); // Number of words per cache line (8 words, since
                              // each word is 4 bytes)
  static uint32_t const way_cnt = ICACHE_V1_WAYS; // N-way set associative cache

  // Table state is intentionally kept outside generalized-IO regs so PI/PO
  // bitvectors do not include the full cache contents.
  uint32_t cache_data[set_num][way_cnt][word_num] = {{{0}}};
  uint32_t cache_tag[set_num][way_cnt] = {{0}};
  bool cache_valid[set_num][way_cnt] = {{false}};

  /*
   * Icache Inner connections between 2 pipeline stages
   */
  struct pipe1_to_pipe2_t {
    // From pipe1 to pipe2 (combination logic/wire)
    bool valid; // Indicates if the data is valid
    uint32_t cache_set_data_w[way_cnt][word_num]; // Data from the cache set
    uint32_t cache_set_tag_w[way_cnt];            // Tag bits from the cache set
    bool cache_set_valid_w[way_cnt]; // Valid bits from the cache set
    uint32_t pc_w;                   // Request PC
    uint32_t index_w;                // Index extracted from PC, index_bits bit
    // next-valid for pipe register
    bool valid_next;
  };

  struct pipe2_to_pipe1_t {
    // control signals
    bool ready; // Indicates if pipe2 is ready to accept data, set in comb_pipe2
  };

  // pipeline datapath
  pipe1_to_pipe2_t pipe1_to_pipe2{};
  pipe2_to_pipe1_t pipe2_to_pipe1{};
  icache_module_n::ICacheState state_next =
      icache_module_n::IDLE; // Next state of the i-cache

  /*
   * Memory Channels
   */
  // state machine
  icache_module_n::AXIState mem_axi_state_next =
      icache_module_n::AXI_IDLE; // Next state of the memory channel

  // received data from memory
  uint32_t mem_resp_data_w[ICACHE_LINE_SIZE / 4] = {0}; // Data received wire

  // handshake signals
  bool mem_gnt = false;

  /*
   * Replacement policy (Random Replacement in current design)
   */
  uint32_t replace_idx_next = 0;

  // Comb-only flag: load cache set into pipe1_to_pipe2 registers this cycle
  bool sram_load_fire = false;
  bool lookup_pending_next = false;
  uint32_t lookup_index_next = 0;
  uint32_t lookup_pc_next = 0;

  // Lookup helpers (stage1 read)
  void lookup(uint32_t index);
  void lookup_read_set(uint32_t lookup_index, bool gate_valid_with_req);
};
}; // namespace icache_module_n

#endif
