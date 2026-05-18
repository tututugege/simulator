/*
 * Architecture of current I-Cache design:
 * - Set associativity, set count and line size come from include/config.h.
 * - The default/small/medium/large profiles currently configure 32KB:
 *   8-way, 64 sets, 64 bytes per cache line.
 * - Random replacement policy
 * - single registered request context with same-cycle hit bypass
 *
 * Default profile address split:
 * PC[31:12], PC[11:6], PC[5:0]
 * - PC[31:12]: 20-bit Tag
 * - PC[11:6]: 6-bit Index to cache set
 * - PC[5:0]: 6-bit Byte offset within a cache line
 *    - PC[5:2]: 4-bit Word offset within a cache line
 *    - PC[1:0]: 2-bit Byte offset within a word
 *
 * 当前实现显式拆成两段组合逻辑：
 * - comb_lookup_meta(): 只消费 tag/valid，完成命中路选择和 miss 判断
 * - comb_lookup_data(): 在命中路确定后消费单路 data，形成最终 hit 响应
 * - perf 统计单独通过 ICache::perf 导出，不混入广义输入/输出
 * - seq(): 只更新请求上下文、ready 状态和 refill 状态
 */
#ifndef ICACHE_MODULE_H
#define ICACHE_MODULE_H

#include <cstdint>
#include <frontend.h>
#include <iostream>
#include "base_types.h"

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

namespace icache_module_n {
// -----------------------------------------------------------------------------
// ICache parameters come from include/config.h.
// -----------------------------------------------------------------------------
static constexpr uint32_t ICACHE_WORD_BITS = 32;

// i-Cache State
enum ICacheState {
  IDLE,                  // Idle state
  SWAP_IN,               // LLC request in flight while DCache probe is unresolved
  WAIT_DCACHE_AFTER_MEM, // LLC response is buffered; wait for DCache probe result
  DRAIN,                 // Draining memory response after refetch
  CANCEL_WAIT_ACCEPT // Wait one cycle for delayed accepted pulse
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
  // Tag/valid response for the looked-up set. Tag match stays inside module.
  wire<1> meta_resp_valid = false;
  wire<ICACHE_TAG_BITS> lookup_set_tag[ICACHE_WAY_NUM] = {0};
  wire<1> lookup_set_valid[ICACHE_WAY_NUM] = {false};
  // Single-way data response selected after the module finishes tag compare.
  wire<1> data_resp_valid = false;
  wire<8> data_resp_way = 0;
  wire<ICACHE_WORD_BITS> data_resp_line[ICACHE_WORD_NUM] = {0};
};

// -----------------------------------------------------------------------------
// Generalized-IO: register state (sequential) excluding perf counters
// -----------------------------------------------------------------------------
struct ICache_regs_t {
  // Registered request context for the current in-flight lookup/fill.
  reg<1> req_valid_r = false;
  reg<32> req_pc_r = 0;
  reg<ICACHE_INDEX_BITS> req_index_r = 0;
  // Registered frontend acceptance state.
  reg<1> ifu_req_ready_r = true;

  // FSM + memory channel registers
  reg<3> state = static_cast<reg<3>>(IDLE);
  reg<1> mem_axi_state = static_cast<reg<1>>(AXI_IDLE);

  // Memory response registers
  reg<ICACHE_WORD_BITS> mem_resp_data_r[ICACHE_WORD_NUM] = {0};

  // Replacement / translation state
  reg<8> replace_idx = 0;
  reg<20> ppn_r = 0;
  reg<1> miss_txid_valid_r = false;
  reg<4> miss_txid_r = 0;
  reg<1> miss_ready_seen_r = false;
  reg<1> txid_inflight_r[16] = {false};
  reg<1> txid_canceled_r[16] = {false};
  reg<1> dcache_probe_inflight_r = false;
  reg<1> dcache_probe_done_r = false;
  reg<1> dcache_probe_hit_r = false;
  reg<5> dcache_probe_word_r = 0;
  reg<ICACHE_WORD_BITS> dcache_probe_data_r[ICACHE_WORD_NUM] = {0};

  // Lookup in-flight state (resource state, not SRAM implementation timing).
  reg<1> lookup_pending_r = false;
  reg<ICACHE_INDEX_BITS> lookup_index_r = 0;
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
  wire<1> invalidate_all = false;
  wire<ICACHE_INDEX_BITS> index = 0;
  wire<8> way = 0;
  wire<ICACHE_WORD_BITS> data[ICACHE_WORD_NUM] = {0};
  wire<ICACHE_TAG_BITS> tag = 0;
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
  wire<1> mem_req_accepted = false;
  wire<4> mem_req_accepted_id = 0;
  wire<1> mem_resp_valid = false;
  // For compatibility with the top-level memory response wiring.
  wire<4> mem_resp_id = 0;
  wire<ICACHE_WORD_BITS> mem_resp_data[ICACHE_WORD_NUM] = {0}; // Data from memory (Cache line)
  // Input from DCache coherent word probe path.
  wire<1> dcache_resp_valid = false;
  wire<1> dcache_resp_miss = true;
  wire<ICACHE_WORD_BITS> dcache_resp_data = 0;
};

struct ICache_out_t {
  // Output to the IFU (Instruction Fetch Unit)
  wire<1> miss = false;           // Cache miss signal
  wire<1> ifu_resp_valid = false; // Indicates if output data is valid
  wire<1> ifu_req_ready = false;  // Indicates if i-cache is allow to accept next PC
  wire<32> ifu_resp_pc = 0;    // PC corresponding to ifu_resp
  wire<ICACHE_WORD_BITS> rd_data[ICACHE_WORD_NUM] = {0}; // Data read from cache
  wire<1> ifu_page_fault = false;                 // page fault exception signal

  // Output to MMU (Memory Management Unit)
  wire<1> ppn_ready = false; // ready to accept PPN
  // MMU request (drive vtag for translation; unify with ICacheV2 top wiring)
  wire<1> mmu_req_valid = false;
  wire<20> mmu_req_vtag = 0;

  // Output to memory
  wire<1> mem_req_valid = false;    // Memory request signal
  wire<32> mem_req_addr = 0;     // Address for memory access
  wire<4> mem_req_id = 0;        // Memory transaction ID
  wire<1> mem_resp_ready = false;
  // DCache coherent word probe generated on an icache miss.
  wire<1> dcache_req_valid = false;
  wire<32> dcache_req_addr = 0;
  // External single-way data query generated after internal tag compare.
  wire<1> lookup_data_req_valid = false;
  wire<ICACHE_INDEX_BITS> lookup_data_req_index = 0;
  wire<8> lookup_data_req_way = 0;
};

struct ICache_perf_t {
  wire<1> miss_issue_valid = false;
  wire<1> miss_penalty_valid = false;
  wire<64> miss_penalty_cycles = 0;
  wire<1> axi_read_valid = false;
  wire<64> axi_read_cycles = 0;
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
  void comb();
  void comb_lookup_meta();
  void comb_lookup_data();
  void eval_state_machine();
  void seq();

  // IO ports
  ICache_IO_t io;
  ICache_perf_t perf;

  // Debug
  void log_state();
  void log_tag(uint32_t index);
  void log_valid(uint32_t index);
  void log_pipeline();
  int valid_line_num() { return -1; }

private:
  /*
   * Cache parameters
   *
   * offset_bits + index_bits + tag_bits = 32
   * for current design, tag_bits = 20
   */
  static uint32_t const offset_bits = ICACHE_OFFSET_BITS;
  static uint32_t const index_bits = ICACHE_INDEX_BITS;
  static uint32_t const set_num = ICACHE_SET_NUM;
  static uint32_t const word_num = ICACHE_WORD_NUM;
  static uint32_t const way_cnt = ICACHE_WAY_NUM;

  // Folded lookup view used by the request compare path.
  // The module only consumes generalized lookup inputs and never inspects the
  // backing table's full set payload directly.
  bool lookup_hit_valid_w = false;
  bool lookup_data_ready_w = false;
  uint32_t lookup_hit_way_w = 0;
  uint32_t lookup_hit_data_w[word_num] = {0};
  bool lookup_has_invalid_way_w = false;
  uint32_t lookup_first_invalid_way_w = 0;
  bool lookup_data_phase_en = false;

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

  // Lookup / acceptance combinational state
  bool req_ready_w = true;
  bool sram_load_fire = false;
  bool fast_bypass_fire = false;
  bool fast_bypass_from_pending = false;
  bool lookup_pending_next = false;
  uint32_t lookup_index_next = 0;
  uint32_t lookup_pc_next = 0;

  // Perf bookkeeping is simulator-local state, not architectural/register
  // state of the hardware model. Keep it out of generalized IO/reg structs.
  struct PerfState {
    bool miss_penalty_active = false;
    uint64_t miss_penalty_start_cycle = 0;
    bool axi_read_active = false;
    uint64_t axi_read_start_cycle = 0;
  };
  PerfState perf_state = {};
  PerfState perf_state_next = {};

  // Lookup helpers (current-cycle fold from the table response). The comb entry
  // points are pure functions of generalized input/registered state and only
  // differ in whether the second-stage data fold is allowed in that call.
  void comb_core(bool allow_lookup_data_phase);
  void lookup(uint32_t index);
  void capture_lookup_meta_result(uint32_t compare_tag, bool compare_valid);
  void capture_lookup_data_result();
};

}; // namespace icache_module_n

#endif
