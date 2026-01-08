#include "include/icache_module.h"
#include <cstring>
#include <iostream>

using namespace icache_module_n;

// Constructor - initialize cache arrays
ICache::ICache() {
  reset();

  // Initialize cache data, tags, and valid bits
  for (uint32_t i = 0; i < set_num; ++i) {
    for (uint32_t j = 0; j < way_cnt; ++j) {
      cache_valid[i][j] = false; // All cache lines initially invalid
      cache_tag[i][j] = 0;
      for (uint32_t k = 0; k < word_num; ++k) {
        cache_data[i][j][k] = 0;
      }
    }
  }

  // Initialize other state variables
  state = IDLE;
  state_next = IDLE;
  mem_axi_state = AXI_IDLE;
  mem_axi_state_next = AXI_IDLE;
  replace_idx = 0;
  replace_idx_next = 0;
  ppn_r = 0;
  mem_gnt = 0;
  pipe1_to_pipe2.valid_r = false;
  pipe2_to_pipe1.ready = true; // initially ready
}

void ICache::reset() {
  // Initialize other state variables
  state = IDLE;
  state_next = IDLE;
  mem_axi_state = AXI_IDLE;
  mem_axi_state_next = AXI_IDLE;
  replace_idx = 0;
  replace_idx_next = 0;
  ppn_r = 0;
  mem_gnt = 0;
  pipe1_to_pipe2.valid_r = false;
  pipe2_to_pipe1.ready = true; // initially ready
}

void ICache::set_refetch() {
  // 出现了重取指令信号，重新设置状态
  state = IDLE;
  mem_axi_state = AXI_IDLE;
  pipe1_to_pipe2.valid_r = false;
  pipe2_to_pipe1.ready = true; // initially ready
}

void ICache::comb() {
  // Implementation of combinational logic
  //
  // Using While loop to ensure combinational logic converges
  // (when pipe1_to_pipe2 and pipe2_to_pipe1 do not change anymore)
  pipe1_to_pipe2_t pipe1_to_pipe2_last = pipe1_to_pipe2;
  pipe2_to_pipe1_t pipe2_to_pipe1_last = pipe2_to_pipe1;
  int cnt = 0; // counter for number of iterations
  while (true) {
    comb_pipe1();
    comb_pipe2();

    // Compare each field of the structs manually
    bool pipe1_equal = pipe1_to_pipe2_last.valid == pipe1_to_pipe2.valid &&
                       pipe1_to_pipe2_last.index_w == pipe1_to_pipe2.index_w;

    bool pipe2_equal = pipe2_to_pipe1_last.ready == pipe2_to_pipe1.ready;

    // Additional field comparisons as necessary for pipe1_to_pipe2
    for (uint32_t way = 0; way < way_cnt; ++way) {
      for (uint32_t word = 0; word < word_num; ++word) {
        pipe1_equal =
            pipe1_equal && (pipe1_to_pipe2_last.cache_set_data_w[way][word] ==
                            pipe1_to_pipe2.cache_set_data_w[way][word]);
      }
      pipe1_equal = pipe1_equal &&
                    (pipe1_to_pipe2_last.cache_set_tag_w[way] ==
                     pipe1_to_pipe2.cache_set_tag_w[way]) &&
                    (pipe1_to_pipe2_last.cache_set_valid_w[way] ==
                     pipe1_to_pipe2.cache_set_valid_w[way]);
    }

    if (pipe1_equal && pipe2_equal) {
      break; // signals have converged
    }
    pipe1_to_pipe2_last = pipe1_to_pipe2;
    pipe2_to_pipe1_last = pipe2_to_pipe1;

    // for debug: avoid infinite loop
    if (++cnt > 20) {
      std::cerr << "Warning: ICache combinational logic did not converge after "
                   "20 iterations. "
                << "Check for combinational loops." << std::endl;
      exit(1);
    }
  }
}

void ICache::seq() {
  // Implementation of sequential logic
  seq_pipe1();
}

/*
 * 第一级流水线：取到IFU的PC对应Index 的 cache set
 *
 * Input:
 * - (IFU) io.in.pc: used to extract index from virtual address
 * - (IFU) valid_in: indicates if the input is valid
 * - (pipe2) ready_in: indicates if pipe2 is ready to accept data
 *
 * Output:
 * - (IFU) ready_out: indicates if the output is ready
 * - (pipe2) valid_out: indicates if the output is valid
 * - (pipe2) cache_set_data: data from the cache set corresponding to the index
 * extracted from PC
 * - (pipe2) cache_set_tag: tag from the cache set corresponding to the index
 * extracted from PC
 * - (pipe2) cache_set_valid: valid bits from the cache set corresponding to the
 *
 */
void ICache::comb_pipe1() {
  /*
   * Channel 1: pipe1 to pipe2
   */
  // Extract index from PC
  uint32_t index = (io.in.pc >> offset_bits) & (set_num - 1);

  // get cache set data, tag, and valid bits
  for (uint32_t way = 0; way < way_cnt; ++way) {
    for (uint32_t word = 0; word < word_num; ++word) {
      pipe1_to_pipe2.cache_set_data_w[way][word] = cache_data[index][way][word];
    }
    pipe1_to_pipe2.cache_set_tag_w[way] = cache_tag[index][way];
    // pipe1_to_pipe2.cache_set_valid_w[way] = cache_valid[index][way];
    pipe1_to_pipe2.cache_set_valid_w[way] =
        cache_valid[index][way] &&
        io.in.ifu_req_valid; // only valid when ifu_req_valid is true
  }
  pipe1_to_pipe2.index_w = index;

  // Set output valid and ready signals
  pipe1_to_pipe2.valid = io.in.ifu_req_valid;

  /*
   * Channel 2: pipe1 to IFU
   */
  // Set ready signal for IFU - only ready when pipe2 is ready to accept data
  io.out.ifu_req_ready = pipe2_to_pipe1.ready;
}

/*
 * 第二级流水线：在该 cache set 中查找 Tag，决定是否命中
 *
 * Input:
 * - (pipe1) valid_in: indicates if the input is valid
 * - (pipe1) cache_set_data: data from the cache set
 * - (pipe1) cache_set_tag: tag bits
 * - (pipe1) cache_set_valid: valid bits
 * - (MMU) io.in.ppn: Physical Page Number (As Tag)
 * - (MMU) io.in.ppn_valid:
 * - (MEM) io.in.mem_req_ready: Memory request ready signal
 * - (MEM) io.in.mem_resp_valid: Memory response valid signal
 * - (MEM) io.in.mem_resp_data: Data from memory (size of a cache line)
 *
 * Output:
 * - (pipe1) ready_out: used to
 * - (MMU) ready_out: indicates if the icache is ready to accept PPN
 * - (MEM) io.out.mem_req_valid: Memory request signal
 * - (MEM) io.out.mem_req_addr: Address for memory access
 * - (MEM) io.out.mem_resp_ready: Memory response ready signal
 * - (IFU) io.out.ifu_resp_valid: indicates if output data is valid
 * - (IFU) io.out.rd_data: Data read from cache (size of a cache line)
 */
void ICache::comb_pipe2() {
  // set default output values (which will be used in most cases)
  io.out.ppn_ready = false;
  io.out.mem_resp_ready = false;
  io.out.mem_req_valid = false;
  io.out.ifu_resp_valid = false;
  io.out.ifu_page_fault = false; // default: no page fault
  mem_axi_state_next = mem_axi_state;
  pipe2_to_pipe1.ready = false;

  switch (state) {
  case IDLE:
    // Check if :
    // - pipe2 already has valid data
    // - ppn from MMU is valid
    if (pipe1_to_pipe2.valid_r && io.in.ppn_valid) {
      // deal with page fault
      if (io.in.page_fault) {
        for (uint32_t word = 0; word < word_num; ++word) {
          io.out.rd_data[word] = 0; // return NOPs
        }
        io.out.ifu_resp_valid = true; // response valid signal for IFU
        io.out.ifu_page_fault = true; // page fault signal for IFU
        pipe2_to_pipe1.ready = true;  // ready for next input
        state_next = IDLE;
        return;
      }
      // Check if the tag matches
      bool hit = false;
      for (uint32_t way = 0; way < way_cnt; ++way) {
        if (pipe1_to_pipe2.cache_set_valid_r[way] &&
            pipe1_to_pipe2.cache_set_tag_r[way] == (io.in.ppn & 0xFFFFF)) {
          hit = true;
          // Read data from cache
          for (uint32_t word = 0; word < word_num; ++word) {
            io.out.rd_data[word] = pipe1_to_pipe2.cache_set_data_r[way][word];
          }
          break;
        }
      }
      // set wire signals
      io.out.ifu_resp_valid = hit; // response valid signal for IFU
      pipe2_to_pipe1.ready = hit;  // only ready when hit
      state_next = hit ? IDLE : SWAP_IN;
    } else if (pipe1_to_pipe2.valid_r && !io.in.ppn_valid) {
      // waiting for valid input from MMU
      pipe2_to_pipe1.ready = false;
      state_next = IDLE;
    } else {
      // waiting for valid input from pipe1
      pipe2_to_pipe1.ready = true;
      state_next = IDLE;
    }
    // 默认只有在 SWAP_IN 状态下才会向 memory 发起请求
    io.out.mem_req_valid = false;
    io.out.mem_resp_ready = false;
    // only accept new ppn when pipe2 is not holding valid data
    io.out.ppn_ready = pipe1_to_pipe2.valid_r;
    break;

  case SWAP_IN:
    if (mem_axi_state == AXI_IDLE) {
      /*
       * Try to send memory request to memory
       */
      io.out.mem_req_valid = true;
      io.out.mem_req_addr =
          (ppn_r << 12) | (pipe1_to_pipe2.index_r << offset_bits);
      // Set next state to SWAP_IN_OKEY
      state_next = SWAP_IN;
      mem_axi_state_next =
          (io.out.mem_req_valid && io.in.mem_req_ready) ? AXI_BUSY : AXI_IDLE;
    } else {
      /*
       * waiting for memory response
       */
      io.out.mem_req_valid = false;
      io.out.mem_resp_ready = true;

      mem_gnt = io.in.mem_resp_valid && io.out.mem_resp_ready;
      state_next = SWAP_IN;
      // If memory response is valid, read data from memory
      if (mem_gnt) {
        // Read data from memory response
        state_next = SWAP_IN_OKEY;
        mem_axi_state_next = AXI_IDLE;
        for (uint32_t offset = 0; offset < ICACHE_LINE_SIZE / 4; ++offset) {
          mem_resp_data_w[offset] = io.in.mem_resp_data[offset];
        }
        // generate output data to ifu
        // TODO: decide where to store via LRU algorithm
        // lru.comb(); // 需要实现的效果：获得一个可用的 replace_way_idx
        //
        // 临时方案：先看该 index 中有无无效路，有则替换无效路；否则随机替换
        bool found_invalid = false;
        for (uint32_t way = 0; way < way_cnt; ++way) {
          if (!pipe1_to_pipe2.cache_set_valid_r[way]) {
            replace_idx_next = way;
            found_invalid = true;
            break;
          }
        }
        if (!found_invalid) {
          replace_idx_next = (replace_idx + 1) % way_cnt;
        }
      }
    }
    break;

  case SWAP_IN_OKEY:
    /*
     * Memory data has been received, update cache and return to IDLE state
     *
     * forward pass the cacheline data to IFU
     */
    state_next = IDLE;
    io.out.ifu_resp_valid = true; // forward pass
    for (uint32_t word = 0; word < word_num; ++word) {
      io.out.rd_data[word] = mem_resp_data_r[word];
    }
    break;

  default:
    // Do not expect to reach here
    std::cerr << "Error: Invalid state in ICache::comb_pipe2()" << std::endl;
    exit(1);
    break;
  }
}

/*
 * 第一级seq流水线：更新 cache set、Cache 状态、
 */
void ICache::seq_pipe1() {
  // pipe1 to pipe2 data transfer
  //
  // 仅当 IDLE 状态才允许 pipe1_to_pipe2 传输
  if (state == IDLE) {
    // clear valid_r in IDLE state by default
    pipe1_to_pipe2.valid_r = false;
  }
  if (pipe1_to_pipe2.valid && pipe2_to_pipe1.ready) {
    // Register the data from pipe1 to pipe2
    pipe1_to_pipe2.valid_r = true;
    for (uint32_t way = 0; way < way_cnt; ++way) {
      for (uint32_t word = 0; word < word_num; ++word) {
        pipe1_to_pipe2.cache_set_data_r[way][word] =
            pipe1_to_pipe2.cache_set_data_w[way][word];
      }
      pipe1_to_pipe2.cache_set_tag_r[way] = pipe1_to_pipe2.cache_set_tag_w[way];
      pipe1_to_pipe2.cache_set_valid_r[way] =
          pipe1_to_pipe2.cache_set_valid_w[way];
    }
    pipe1_to_pipe2.index_r = pipe1_to_pipe2.index_w;
  } else if (!pipe2_to_pipe1.ready) {
    // hold the data in pipe2 when pipe2 is not ready
    pipe1_to_pipe2.valid_r = true;
  }

  if (io.in.ppn_valid && io.out.ppn_ready) {
    // store used ppn, which will be used in SWAP_IN_OKEY state
    ppn_r = io.in.ppn;
  }

  switch (state) {
  case IDLE:
    /* No registers to update */
    break;

  case SWAP_IN:
    /* save data retrievd from memory */
    for (uint32_t offset = 0; offset < ICACHE_LINE_SIZE / 4; ++offset) {
      mem_resp_data_r[offset] = mem_resp_data_w[offset];
    }
    replace_idx = replace_idx_next;
    break;

  case SWAP_IN_OKEY:
    /* update cache with new data from memory */
    for (uint32_t word = 0; word < word_num; ++word) {
      cache_data[pipe1_to_pipe2.index_r][replace_idx][word] =
          mem_resp_data_r[word];
    }
    cache_tag[pipe1_to_pipe2.index_r][replace_idx] = ppn_r;
    cache_valid[pipe1_to_pipe2.index_r][replace_idx] = true;
    pipe1_to_pipe2.valid_r = false; // after update, invalidate pipe2 register
                                    // to force next fetch from pipe1
    break;

  default:
    break;
  }
  state = state_next;
  mem_axi_state = mem_axi_state_next;
}

/*
 * Debug functions to log internal state
 */
void ICache::log_state() {
  std::cout << "ICache State: ";
  switch (state) {
  case IDLE:
    std::cout << "IDLE";
    break;
  case SWAP_IN:
    std::cout << "SWAP_IN";
    break;
  case SWAP_IN_OKEY:
    std::cout << "SWAP_IN_OKEY";
    break;
  default:
    std::cout << "UNKNOWN";
    break;
  }
  std::cout << " -> ";
  switch (state_next) {
  case IDLE:
    std::cout << "IDLE";
    break;
  case SWAP_IN:
    std::cout << "SWAP_IN";
    break;
  case SWAP_IN_OKEY:
    std::cout << "SWAP_IN_OKEY";
    break;
  default:
    std::cout << "UNKNOWN";
    break;
  }
  std::cout << std::endl;
}
void ICache::log_tag(uint32_t index) {
  if (index >= set_num) {
    std::cerr << "Index out of bounds in log_tag: " << index << std::endl;
    return;
  }
  std::cout << "Cache Set Index: " << index << std::endl;
  for (uint32_t way = 0; way < way_cnt; ++way) {
    std::cout << "  Way " << way << ": Valid=" << cache_valid[index][way]
              << ", Tag=0x" << std::hex << cache_tag[index][way] << std::dec
              << ", Data=[";
    for (uint32_t word = 0; word < word_num; ++word) {
      if (word > 0)
        std::cout << ", ";
      std::cout << "0x" << std::hex << cache_data[index][way][word] << std::dec;
    }
    std::cout << "]" << std::endl;
  }
}
void ICache::log_valid(uint32_t index) {
  if (index >= set_num) {
    std::cerr << "Index out of bounds in log_valid: " << index << std::endl;
    return;
  }
  std::cout << "Cache Set Index: " << index << " Valid Bits: ";
  for (uint32_t way = 0; way < way_cnt; ++way) {
    std::cout << cache_valid[index][way] << " ";
  }
  std::cout << std::endl;
}
void ICache::log_pipeline() {
  std::cout << "Pipeline Registers:" << std::endl;
  std::cout << "  pipe1_to_pipe2.valid_r: " << pipe1_to_pipe2.valid_r
            << std::endl;
  std::cout << "  pipe1_to_pipe2.index_r: " << pipe1_to_pipe2.index_r
            << std::endl;
  std::cout << "  ppn_r: 0x" << std::hex << ppn_r << std::dec << std::endl;
  for (uint32_t way = 0; way < way_cnt; ++way) {
    std::cout << "  Way " << way
              << ": Valid=" << pipe1_to_pipe2.cache_set_valid_r[way]
              << ", Tag=0x" << std::hex << pipe1_to_pipe2.cache_set_tag_r[way]
              << std::dec << ", Data=[";
    for (uint32_t word = 0; word < word_num; ++word) {
      if (word > 0)
        std::cout << ", ";
      std::cout << "0x" << std::hex
                << pipe1_to_pipe2.cache_set_data_r[way][word] << std::dec;
    }
    std::cout << "]" << std::endl;
  }
}
