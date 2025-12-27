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

#include <iostream>
#include <cstdint>
#include <frontend.h>

#define ICACHE_LINE_SIZE 32 // Size of a cache line in bytes

namespace icache_module_n {
  // i-Cache State
  enum ICacheState {
    IDLE,         // Idle state
    SWAP_IN,      // Swapping in state
    SWAP_IN_OKEY, // Swapping in successful
  };
  // AXI Memory Channel State
  enum AXIState {
    AXI_IDLE,     // Idle state
    AXI_BUSY,     // Busy state
  };
};

struct ICache_in_t {
  // Input from the IFU (Instruction Fetch Unit)
  uint32_t pc;          // Program Counter
  bool ifu_req_valid;   // Fetch enable signal
  bool ifu_resp_ready;  // actually always true in current design

  // Input from MMU (Memory Management Unit)
  uint32_t ppn;     // Physical Page Number
  bool ppn_valid;   // PPN valid signal
  bool page_fault;       // page fault exception signal

  // Input from memory
  bool mem_req_ready;
  bool mem_resp_valid;
  uint32_t mem_resp_data [ICACHE_LINE_SIZE / 4]; // Data from memory (32 bytes)
};

struct ICache_out_t {
  // Output to the IFU (Instruction Fetch Unit)
  bool miss; // Cache miss signal
  bool ifu_resp_valid; // Indicates if output data is valid
  bool ifu_req_ready; // Indicates if i-cache is allow to accept next PC
  uint32_t rd_data [ICACHE_LINE_SIZE / 4]; // Data read from cache
  bool ifu_page_fault; // page fault exception signal

  // Output to MMU (Memory Management Unit)
  bool ppn_ready; // ready to accept PPN

  // Output to memory
  bool mem_req_valid; // Memory request signal
  uint32_t mem_req_addr; // Address for memory access
  bool mem_resp_ready;
};

// cache io
struct ICache_IO_t {
  ICache_in_t in;
  ICache_out_t out;
};

class ICache {
public:
  // Constructor
  ICache();

  void reset();
  void set_refetch();
  void comb();
  void comb_pipe1();
  void comb_pipe2();
  void seq();
  void seq_pipe1();

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
   */
  static uint32_t const offset_bits = 5; // 32 bytes per cache line
  static uint32_t const index_bits = 7; // 128 sets in the cache
  static uint32_t const set_num = 1 << index_bits; // Total number of cache sets 
  static uint32_t const word_num = 1 << (offset_bits - 2); // Number of words per cache line (8 words, since each word is 4 bytes)
  static uint32_t const line_size = 1 << offset_bits;
  static uint32_t const way_cnt = 8; // 8-way set associative cache
  uint32_t cache_data[set_num][way_cnt][word_num]; // Cache data storage
  uint32_t cache_tag[set_num][way_cnt]; // Cache tags
  bool cache_valid[set_num][way_cnt]; // Valid bits for each cache line

  /*
   * Icache Inner connections between 2 pipeline stages
   */
  struct pipe1_to_pipe2_t {
    // From pipe1 to pipe2 (combination logic/wire)
    bool valid; // Indicates if the data is valid
    uint32_t cache_set_data_w[way_cnt][word_num]; // Data from the cache set
    uint32_t cache_set_tag_w[way_cnt]; // Tag bits from the cache set
    bool cache_set_valid_w[way_cnt]; // Valid bits from the cache set
    uint32_t index_w; // Index extracted from PC, index_bits bit
    // Registered data (between two pipeline stages)
    bool valid_r;
    uint32_t cache_set_data_r[way_cnt][word_num]; 
    uint32_t cache_set_tag_r[way_cnt]; 
    bool cache_set_valid_r[way_cnt];
    uint32_t index_r;
  };

  struct pipe2_to_pipe1_t {
    // control signals
    bool ready; // Indicates if pipe2 is ready to accept data, set in comb_pipe2
  };

  // pipeline datapath
  pipe1_to_pipe2_t pipe1_to_pipe2;
  pipe2_to_pipe1_t pipe2_to_pipe1;

  icache_module_n::ICacheState state = icache_module_n::IDLE; // Current state of the i-cache
  icache_module_n::ICacheState state_next = icache_module_n::IDLE; // Next state of the i-cache

  /*
   * Memory Channels
   */
  // state machine
  icache_module_n::AXIState mem_axi_state = icache_module_n::AXI_IDLE; // Current state of the memory channel
  icache_module_n::AXIState mem_axi_state_next = icache_module_n::AXI_IDLE; // Current state of the memory channel

  // received data from memory
  uint32_t mem_resp_data_w[ICACHE_LINE_SIZE / 4]; // Data received wire
  uint32_t mem_resp_data_r[ICACHE_LINE_SIZE / 4]; // Data received register

  // handshake signals
  bool mem_gnt;

  /*
   * Replacement policy (Random Replacement in current design)
   */
  uint32_t replace_idx = 0;
  uint32_t replace_idx_next;
  uint32_t ppn_r; // length = paddr_length(32/34) - 12 bits = 20/22 bits
};

#endif