#include "include/icache_module.h"
#include <iostream>

using namespace icache_module_n;

namespace {
inline bool lookup_latency_enabled() { return ICACHE_LOOKUP_LATENCY > 0; }
inline bool mem_wait_must_drain_on_refetch() {
  return true;
}

inline int alloc_free_txid(const ICache_regs_t &regs) {
  for (int id = 0; id < 16; ++id) {
    if (!regs.txid_inflight_r[id]) {
      return id;
    }
  }
  return -1;
}
} // namespace

ICache::ICache() {
  reset();

  // Initialize cache data, tags, and valid bits
  for (uint32_t i = 0; i < set_num; ++i) {
    for (uint32_t j = 0; j < way_cnt; ++j) {
      cache_valid[i][j] = false;
      cache_tag[i][j] = 0;
      for (uint32_t k = 0; k < word_num; ++k) {
        cache_data[i][j][k] = 0;
      }
    }
  }

  // Initialize state variables
  io.regs.state = static_cast<uint8_t>(IDLE);
  state_next = IDLE;
  io.regs.mem_axi_state = static_cast<uint8_t>(AXI_IDLE);
  mem_axi_state_next = AXI_IDLE;
  io.regs.replace_idx = 0;
  replace_idx_next = 0;
  io.regs.ppn_r = 0;
  io.regs.ifu_req_ready_r = true;
  io.regs.miss_txid_valid_r = false;
  io.regs.miss_txid_r = 0;
  for (int i = 0; i < 16; ++i) {
    io.regs.txid_inflight_r[i] = false;
    io.regs.txid_canceled_r[i] = false;
  }
  mem_gnt = 0;
  io.regs.req_valid_r = false;
  io.regs.req_pc_r = 0;
  io.regs.req_index_r = 0;
  req_ready_w = true;

  io.regs.lookup_pending_r = false;
  lookup_pending_next = false;
  io.regs.lookup_index_r = 0;
  lookup_index_next = 0;
  io.regs.lookup_pc_r = 0;
  lookup_pc_next = 0;
  sram_load_fire = false;
}

void ICache::reset() {
  io.regs.state = static_cast<uint8_t>(IDLE);
  state_next = IDLE;
  io.regs.mem_axi_state = static_cast<uint8_t>(AXI_IDLE);
  mem_axi_state_next = AXI_IDLE;
  io.regs.replace_idx = 0;
  replace_idx_next = 0;
  io.regs.ppn_r = 0;
  io.regs.ifu_req_ready_r = true;
  io.regs.miss_txid_valid_r = false;
  io.regs.miss_txid_r = 0;
  for (int i = 0; i < 16; ++i) {
    io.regs.txid_inflight_r[i] = false;
    io.regs.txid_canceled_r[i] = false;
  }
  mem_gnt = 0;
  io.regs.req_valid_r = false;
  io.regs.req_pc_r = 0;
  io.regs.req_index_r = 0;
  req_ready_w = true;

  io.regs.lookup_pending_r = false;
  lookup_pending_next = false;
  io.regs.lookup_index_r = 0;
  lookup_index_next = 0;
  io.regs.lookup_pc_r = 0;
  lookup_pc_next = 0;
  sram_load_fire = false;

  if (lookup_latency_enabled()) {
    std::cout << "[icache] lookup source: external delayed response"
              << std::endl;
  } else {
    std::cout << "[icache] lookup source: register-style internal set read"
              << std::endl;
  }
}

void ICache::invalidate_all() {
  for (uint32_t i = 0; i < set_num; ++i) {
    for (uint32_t j = 0; j < way_cnt; ++j) {
      cache_valid[i][j] = false;
    }
  }
}

void ICache::comb() {
  // Initialize generalized outputs at the start of comb().
  io.out = {};
  io.table_write = {};
  fast_bypass_fire = false;
  fast_bypass_from_pending = false;
  io.out.ifu_req_ready = io.regs.ifu_req_ready_r;
  io.out.ifu_resp_pc = io.regs.req_pc_r;
  io.out.mem_req_id = io.regs.miss_txid_valid_r ? io.regs.miss_txid_r : 0;
  io.out.mem_req_addr =
      (io.regs.ppn_r << 12) | (io.regs.req_index_r << offset_bits);
  if (io.regs.lookup_pending_r) {
    io.out.mmu_req_vtag = io.regs.lookup_pc_r >> 12;
  } else if (io.regs.req_valid_r) {
    io.out.mmu_req_vtag = io.regs.req_pc_r >> 12;
  } else {
    io.out.mmu_req_vtag = io.in.pc >> 12;
  }

  // Default register write-back: hold values unless overwritten by comb logic.
  io.reg_write = io.regs;
  req_ready_w = io.regs.ifu_req_ready_r;

  // Ordered comb interaction:
  // 1) state machine computes hit/miss/fill behavior from registered request
  //    context and current memory/MMU inputs
  // 2) request side updates next request/lookup state
  eval_state_machine();
  uint32_t index = (io.in.pc >> offset_bits) & (set_num - 1u);
  lookup(index);
}

void ICache::seq() {
  if (io.in.flush) {
    invalidate_all();
  }

  if (io.table_write.we && !io.in.flush) {
    uint32_t index = io.table_write.index;
    uint32_t way = io.table_write.way;
    if (index < set_num && way < way_cnt) {
      for (uint32_t word = 0; word < word_num; ++word) {
        cache_data[index][way][word] = io.table_write.data[word];
      }
      cache_tag[index][way] = io.table_write.tag;
      cache_valid[index][way] = io.table_write.valid;
    }
  }

  io.regs = io.reg_write;
}

void ICache::export_lookup_set_for_pc(
    uint32_t pc, uint32_t out_data[ICACHE_V1_WAYS][ICACHE_LINE_SIZE / 4],
    uint32_t out_tag[ICACHE_V1_WAYS], bool out_valid[ICACHE_V1_WAYS]) const {
  uint32_t pc_index = (pc >> offset_bits) & (set_num - 1u);
  uint32_t rd_index = pc_index;
  if (lookup_latency_enabled()) {
    rd_index = io.regs.lookup_pending_r ? io.regs.lookup_index_r : pc_index;
  }

  for (uint32_t way = 0; way < way_cnt; ++way) {
    for (uint32_t word = 0; word < word_num; ++word) {
      out_data[way][word] = cache_data[rd_index][way][word];
    }
    out_tag[way] = cache_tag[rd_index][way];
    out_valid[way] = cache_valid[rd_index][way];
  }
}

void ICache::lookup_read_set(uint32_t lookup_index, bool gate_valid_with_req) {
  constexpr bool from_input =
      (ICACHE_LOOKUP_FROM_INPUT != 0) || (ICACHE_LOOKUP_LATENCY > 0);
  if (from_input && !io.lookup_in.lookup_resp_valid) {
    return;
  }
  for (uint32_t way = 0; way < way_cnt; ++way) {
    for (uint32_t word = 0; word < word_num; ++word) {
      lookup_set_data_w[way][word] =
          from_input ? io.lookup_in.lookup_set_data[way][word]
                     : cache_data[lookup_index][way][word];
    }
    lookup_set_tag_w[way] =
        from_input ? io.lookup_in.lookup_set_tag[way]
                   : cache_tag[lookup_index][way];
    bool valid_bit = from_input ? io.lookup_in.lookup_set_valid[way]
                                : cache_valid[lookup_index][way];
    if (gate_valid_with_req) {
      valid_bit = valid_bit && io.in.ifu_req_valid;
    }
    lookup_set_valid_w[way] = valid_bit;
  }
}

void ICache::lookup(uint32_t index) {
  const bool use_external_lookup = lookup_latency_enabled();
  const bool kill_pipe = io.in.refetch;

  lookup_pending_next = io.regs.lookup_pending_r;
  lookup_index_next = io.regs.lookup_index_r;
  lookup_pc_next = io.regs.lookup_pc_r;
  sram_load_fire = false;
  uint32_t load_pc = io.regs.lookup_pending_r ? io.regs.lookup_pc_r : io.in.pc;
  uint32_t load_index = index;
  bool req_valid_next = io.regs.req_valid_r;

  if (!use_external_lookup) {
    lookup_read_set(index, /*gate_valid_with_req=*/false);
  } else if (io.regs.lookup_pending_r && io.lookup_in.lookup_resp_valid) {
    lookup_read_set(io.regs.lookup_index_r, /*gate_valid_with_req=*/false);
  }

  if (kill_pipe) {
    req_valid_next = false;
    io.out.mmu_req_valid = false;
    io.reg_write.req_valid_r = false;
    io.reg_write.lookup_pending_r = false;
    io.reg_write.lookup_index_r = 0;
    io.reg_write.lookup_pc_r = 0;
    io.reg_write.ifu_req_ready_r = false;
    return;
  }

  if (fast_bypass_fire) {
    req_valid_next = false;
    io.reg_write.req_valid_r = false;
    io.reg_write.lookup_pending_r = false;
    io.reg_write.lookup_index_r = 0;
    io.reg_write.lookup_pc_r = 0;
    io.out.mmu_req_valid = false;
    io.reg_write.ifu_req_ready_r = !fast_bypass_from_pending;
    return;
  }

  bool can_accept = io.in.ifu_req_valid && io.regs.ifu_req_ready_r &&
                    !io.regs.lookup_pending_r;
  if (can_accept) {
    if (!use_external_lookup) {
      sram_load_fire = true;
    } else {
      lookup_pending_next = true;
      lookup_index_next = index;
      lookup_pc_next = io.in.pc;
      if (io.lookup_in.lookup_resp_valid) {
        sram_load_fire = true;
        lookup_pending_next = false;
      }
    }
  }

  if (use_external_lookup && io.regs.lookup_pending_r &&
      io.lookup_in.lookup_resp_valid) {
    sram_load_fire = true;
    lookup_pending_next = false;
  }

  if (sram_load_fire) {
    if (io.regs.lookup_pending_r) {
      load_pc = io.regs.lookup_pc_r;
      load_index = io.regs.lookup_index_r;
    } else {
      load_pc = io.in.pc;
      load_index = index;
    }
    lookup_read_set(load_index, /*gate_valid_with_req=*/false);
  }

  if (sram_load_fire) {
    req_valid_next = true;
  } else if (req_ready_w) {
    req_valid_next = false;
  }

  io.reg_write.req_valid_r = req_valid_next;
  io.reg_write.lookup_pending_r = use_external_lookup ? lookup_pending_next : false;
  io.reg_write.lookup_index_r = use_external_lookup ? lookup_index_next : 0;
  io.reg_write.lookup_pc_r = use_external_lookup ? lookup_pc_next : 0;
  io.reg_write.ifu_req_ready_r =
      req_ready_w && !lookup_pending_next && !req_valid_next;

  if (sram_load_fire && !kill_pipe) {
    io.reg_write.req_pc_r = load_pc;
    io.reg_write.req_index_r = load_index;
  }

  io.out.mmu_req_valid = false;
  if (can_accept) {
    io.out.mmu_req_valid = true;
    io.out.mmu_req_vtag = io.in.pc >> 12;
  } else if (io.regs.lookup_pending_r && !io.in.ppn_valid) {
    io.out.mmu_req_valid = true;
    io.out.mmu_req_vtag = io.regs.lookup_pc_r >> 12;
  } else if (io.regs.req_valid_r && !io.in.ppn_valid) {
    io.out.mmu_req_valid = true;
    io.out.mmu_req_vtag = io.regs.req_pc_r >> 12;
  }
}

void ICache::eval_state_machine() {
  io.table_write.we = false;
  io.table_write.index = 0;
  io.table_write.way = 0;
  for (uint32_t word = 0; word < word_num; ++word) {
    io.table_write.data[word] = 0;
  }
  io.table_write.tag = 0;
  io.table_write.valid = false;

  ICacheState state = static_cast<ICacheState>(io.regs.state);
  AXIState mem_axi_state = static_cast<AXIState>(io.regs.mem_axi_state);
  mem_gnt = false;

  io.out.ppn_ready = false;
  io.out.mem_resp_ready = false;
  io.out.mem_req_valid = false;
  io.out.ifu_resp_valid = false;
  io.out.ifu_resp_pc = io.regs.req_pc_r;
  io.out.ifu_page_fault = false;
  io.out.mem_req_id = io.regs.miss_txid_valid_r ? io.regs.miss_txid_r : 0;
  state_next = state;
  mem_axi_state_next = mem_axi_state;
  req_ready_w = false;
  const bool kill_pipe = io.in.refetch;
  uint8_t resp_id = static_cast<uint8_t>(io.in.mem_resp_id & 0xF);
  bool canceled_resp =
      io.in.mem_resp_valid && io.regs.txid_canceled_r[resp_id];
  bool active_resp =
      io.in.mem_resp_valid && io.regs.miss_txid_valid_r &&
      (resp_id == io.regs.miss_txid_r) && !canceled_resp;

  if (canceled_resp) {
    io.out.mem_resp_ready = true;
    io.reg_write.txid_canceled_r[resp_id] = false;
    io.reg_write.txid_inflight_r[resp_id] = false;
  }

  switch (state) {
  case IDLE:
    if (kill_pipe) {
      state_next = IDLE;
      req_ready_w = true;
      break;
    }

    // Fast hit/pagefault bypass for the no-latency lookup mode when the
    // registered request slot is empty.
    if (!lookup_latency_enabled() && !io.regs.req_valid_r && io.in.ifu_req_valid &&
        io.in.ppn_valid) {
      io.out.ifu_resp_pc = io.in.pc;
      if (io.in.page_fault) {
        for (uint32_t word = 0; word < word_num; ++word) {
          io.out.rd_data[word] = 0;
        }
        io.out.ifu_resp_valid = true;
        io.out.ifu_page_fault = true;
        req_ready_w = true;
        fast_bypass_fire = true;
        state_next = IDLE;
        break;
      }

      uint32_t index = (io.in.pc >> offset_bits) & (set_num - 1u);
      lookup_read_set(index, /*gate_valid_with_req=*/false);
      bool hit = false;
      for (uint32_t way = 0; way < way_cnt; ++way) {
        if (lookup_set_valid_w[way] &&
            lookup_set_tag_w[way] == (io.in.ppn & 0xFFFFF)) {
          hit = true;
          for (uint32_t word = 0; word < word_num; ++word) {
            io.out.rd_data[word] = lookup_set_data_w[way][word];
          }
          break;
        }
      }
      if (hit) {
        io.out.ifu_resp_valid = true;
        io.out.ifu_page_fault = false;
        req_ready_w = true;
        fast_bypass_fire = true;
        state_next = IDLE;
        break;
      }
      // miss falls through to the registered request path below
    }

    if (io.regs.req_valid_r && io.in.ppn_valid) {
      if (io.in.page_fault) {
        for (uint32_t word = 0; word < word_num; ++word) {
          io.out.rd_data[word] = 0;
        }
        io.out.ifu_resp_valid = true;
        io.out.ifu_page_fault = true;
        req_ready_w = true;
        state_next = IDLE;
        break;
      }

      lookup_read_set(io.regs.req_index_r, /*gate_valid_with_req=*/false);
      bool hit = false;
      for (uint32_t way = 0; way < way_cnt; ++way) {
        if (lookup_set_valid_w[way] &&
            lookup_set_tag_w[way] == (io.in.ppn & 0xFFFFF)) {
          hit = true;
          for (uint32_t word = 0; word < word_num; ++word) {
            io.out.rd_data[word] = lookup_set_data_w[way][word];
          }
          break;
        }
      }

      io.out.ifu_resp_valid = hit;
      req_ready_w = hit;
      if (hit) {
        state_next = IDLE;
      } else {
        int txid = io.regs.miss_txid_valid_r ? static_cast<int>(io.regs.miss_txid_r)
                                             : alloc_free_txid(io.regs);
        if (txid < 0) {
          state_next = IDLE;
          req_ready_w = false;
        } else {
          io.reg_write.miss_txid_valid_r = true;
          io.reg_write.miss_txid_r = static_cast<uint8_t>(txid & 0xF);
          state_next = SWAP_IN;
        }
      }

    } else if (io.regs.req_valid_r && !io.in.ppn_valid) {
      req_ready_w = false;
      state_next = IDLE;
    } else {
      req_ready_w = true;
      state_next = IDLE;
    }

    io.out.ppn_ready = io.regs.req_valid_r;
    break;

  case SWAP_IN:
    if (kill_pipe) {
      if (mem_axi_state != AXI_IDLE && mem_wait_must_drain_on_refetch()) {
        state_next = DRAIN;
        req_ready_w = false;
      } else {
        if (mem_axi_state != AXI_IDLE && io.regs.miss_txid_valid_r) {
          uint8_t txid = io.regs.miss_txid_r & 0xF;
          io.reg_write.txid_canceled_r[txid] = true;
        }
        state_next = IDLE;
        mem_axi_state_next = AXI_IDLE;
        io.reg_write.miss_txid_valid_r = false;
        req_ready_w = true;
      }
      break;
    }

    if (mem_axi_state == AXI_IDLE) {
      io.out.mem_req_valid = true;
      io.out.mem_req_addr =
          (io.regs.ppn_r << 12) | (io.regs.req_index_r << offset_bits);
      io.out.mem_req_id = io.regs.miss_txid_r;
      state_next = SWAP_IN;
      if (io.out.mem_req_valid && io.in.mem_req_ready) {
        mem_axi_state_next = AXI_BUSY;
        io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = true;
      } else {
        mem_axi_state_next = AXI_IDLE;
      }
    } else {
      io.out.mem_req_valid = false;
      io.out.mem_resp_ready = io.out.mem_resp_ready || active_resp;

      mem_gnt = active_resp && io.out.mem_resp_ready;
      state_next = SWAP_IN;
      if (mem_gnt) {
        for (uint32_t offset = 0; offset < ICACHE_LINE_SIZE / 4; ++offset) {
          mem_resp_data_w[offset] = io.in.mem_resp_data[offset];
        }

        bool found_invalid = false;
        for (uint32_t way = 0; way < way_cnt; ++way) {
          if (!cache_valid[io.regs.req_index_r][way]) {
            replace_idx_next = way;
            found_invalid = true;
            break;
          }
        }
        if (!found_invalid) {
          replace_idx_next = (io.regs.replace_idx + 1) % way_cnt;
        }

#if ICACHE_V1_DIRECT_REFILL_RESP
        mem_axi_state_next = AXI_IDLE;
        state_next = IDLE;
        io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = false;
        io.reg_write.miss_txid_valid_r = false;

        if (!io.in.flush) {
          io.table_write.we = true;
          io.table_write.index = io.regs.req_index_r;
          io.table_write.way = replace_idx_next;
          for (uint32_t word = 0; word < word_num; ++word) {
            io.table_write.data[word] = mem_resp_data_w[word];
          }
          io.table_write.tag = io.regs.ppn_r;
          io.table_write.valid = true;
        }

        if (!io.in.refetch && !io.in.flush) {
          io.out.ifu_resp_valid = true;
          for (uint32_t word = 0; word < word_num; ++word) {
            io.out.rd_data[word] = mem_resp_data_w[word];
          }
          req_ready_w = true;
        } else {
          req_ready_w = true;
        }
#else
        state_next = SWAP_IN_OKEY;
        mem_axi_state_next = AXI_IDLE;
        io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = false;
#endif
      }
    }
    break;

  case SWAP_IN_OKEY:
    state_next = IDLE;
    io.reg_write.miss_txid_valid_r = false;

    if (!io.in.flush) {
      io.table_write.we = true;
      io.table_write.index = io.regs.req_index_r;
      io.table_write.way = io.regs.replace_idx;
      for (uint32_t word = 0; word < word_num; ++word) {
        io.table_write.data[word] = io.regs.mem_resp_data_r[word];
      }
      io.table_write.tag = io.regs.ppn_r;
      io.table_write.valid = true;
    }

    if (!io.in.refetch && !io.in.flush) {
      io.out.ifu_resp_valid = true;
      for (uint32_t word = 0; word < word_num; ++word) {
        io.out.rd_data[word] = io.regs.mem_resp_data_r[word];
      }
      req_ready_w = true;
    } else {
      req_ready_w = true;
    }
    break;

  case DRAIN:
    if (kill_pipe && !mem_wait_must_drain_on_refetch()) {
      state_next = IDLE;
      mem_axi_state_next = AXI_IDLE;
      io.reg_write.miss_txid_valid_r = false;
      req_ready_w = true;
      break;
    }
    io.out.mem_resp_ready = true;
    io.out.mem_req_valid = false;

    if (io.in.mem_resp_valid) {
      mem_axi_state_next = AXI_IDLE;
      state_next = IDLE;
      if (io.regs.miss_txid_valid_r) {
        uint8_t txid = io.regs.miss_txid_r & 0xF;
        io.reg_write.txid_inflight_r[txid] = false;
        io.reg_write.txid_canceled_r[txid] = false;
        io.reg_write.miss_txid_valid_r = false;
      }
      req_ready_w = true;
    } else {
      state_next = DRAIN;
      req_ready_w = false;
    }
    break;

  default:
    std::cerr << "Error: Invalid state in ICache::eval_state_machine()"
              << std::endl;
    exit(1);
    break;
  }

  // Register write-back (applied in seq).
  io.reg_write.state = static_cast<uint8_t>(state_next);
  io.reg_write.mem_axi_state = static_cast<uint8_t>(mem_axi_state_next);

  // Save PPN (latched on ppn_valid && ppn_ready).
  if (io.in.ppn_valid && io.out.ppn_ready) {
    io.reg_write.ppn_r = io.in.ppn;
  }
  io.reg_write.ifu_req_ready_r = req_ready_w;

  // Latch memory response data + replacement index when the response arrives.
  if (state == SWAP_IN && mem_gnt) {
    for (uint32_t offset = 0; offset < ICACHE_LINE_SIZE / 4; ++offset) {
      io.reg_write.mem_resp_data_r[offset] = mem_resp_data_w[offset];
    }
    io.reg_write.replace_idx = replace_idx_next;
  }

}

void ICache::log_state() {
  ICacheState state = static_cast<ICacheState>(io.regs.state);
  AXIState mem_axi_state = static_cast<AXIState>(io.regs.mem_axi_state);

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
  case DRAIN:
    std::cout << "DRAIN";
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
  case DRAIN:
    std::cout << "DRAIN";
    break;
  default:
    std::cout << "UNKNOWN";
    break;
  }
  std::cout << std::endl;
  std::cout << "  mem_axi_state: " << (mem_axi_state == AXI_IDLE ? "IDLE"
                                                               : "BUSY")
            << " mem_req_v=" << io.out.mem_req_valid
            << " mem_req_rdy=" << io.in.mem_req_ready
            << " mem_resp_v=" << io.in.mem_resp_valid
            << " mem_resp_rdy=" << io.out.mem_resp_ready << std::endl;
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
  std::cout << "Request Context:" << std::endl;
  std::cout << "  req_valid_r: " << io.regs.req_valid_r << std::endl;
  std::cout << "  req_index_r: " << io.regs.req_index_r << std::endl;
  std::cout << "  ppn_r: 0x" << std::hex << io.regs.ppn_r << std::dec << std::endl;
  std::cout << "  ifu_req_ready_r: " << io.regs.ifu_req_ready_r << std::endl;
}
