#include "include/icache_module.h"
#include "config.h"

using namespace icache_module_n;

namespace {
inline bool lookup_latency_enabled() { return ICACHE_LOOKUP_LATENCY > 0; }
inline bool mem_wait_must_drain_on_refetch() {
  return true;
}

#ifndef CONFIG_ICACHE_FOCUS_VADDR_BEGIN
#define CONFIG_ICACHE_FOCUS_VADDR_BEGIN 0u
#endif

#ifndef CONFIG_ICACHE_FOCUS_VADDR_END
#define CONFIG_ICACHE_FOCUS_VADDR_END 0u
#endif

inline bool icache_focus_pc(uint32_t pc) {
  const uint32_t begin = static_cast<uint32_t>(CONFIG_ICACHE_FOCUS_VADDR_BEGIN);
  const uint32_t end = static_cast<uint32_t>(CONFIG_ICACHE_FOCUS_VADDR_END);
  return end > begin && (pc - begin) < (end - begin);
}

inline bool icache_window_debug_active(long long cycle) {
#if SIM_DEBUG_PRINT
  return cycle >= static_cast<long long>(SIM_DEBUG_PRINT_CYCLE_BEGIN) &&
         cycle <= static_cast<long long>(SIM_DEBUG_PRINT_CYCLE_END);
#else
  (void)cycle;
  return false;
#endif
}

inline bool icache_trace_pc(uint32_t pc, long long cycle) {
  return icache_focus_pc(pc) || icache_window_debug_active(cycle);
}

inline void dump_icache_module_line(const char *tag, long long cycle,
                                    const ICache_IO_t &io, bool mem_gnt,
                                    const uint32_t *line_words) {
  std::printf(
      "[ICACHE][MODULE][%s] cyc=%lld state=%u mem_axi=%u req_valid=%u "
      "lookup_pending=%u meta_resp=%u data_resp=%u data_way=%u "
      "req_pc=0x%08x req_idx=%u ppn_r=0x%05x "
      "in_ppn_v=%u in_ppn=0x%05x mmu_req_v=%u mmu_vtag=0x%05x "
      "mem_req_v=%u mem_req_addr=0x%08x miss_txid_v=%u miss_txid=%u "
      "mem_req_acc=%u mem_req_acc_id=%u mem_resp_v=%u mem_resp_id=%u "
      "mem_gnt=%u line=[",
      tag, cycle, static_cast<unsigned>(io.regs.state),
      static_cast<unsigned>(io.regs.mem_axi_state),
      static_cast<unsigned>(io.regs.req_valid_r),
      static_cast<unsigned>(io.regs.lookup_pending_r),
      static_cast<unsigned>(io.lookup_in.meta_resp_valid),
      static_cast<unsigned>(io.lookup_in.data_resp_valid),
      static_cast<unsigned>(io.lookup_in.data_resp_way),
      static_cast<unsigned>(io.regs.req_pc_r),
      static_cast<unsigned>(io.regs.req_index_r),
      static_cast<unsigned>(io.regs.ppn_r),
      static_cast<unsigned>(io.in.ppn_valid),
      static_cast<unsigned>(io.in.ppn),
      static_cast<unsigned>(io.out.mmu_req_valid),
      static_cast<unsigned>(io.out.mmu_req_vtag),
      static_cast<unsigned>(io.out.mem_req_valid),
      static_cast<unsigned>(io.out.mem_req_addr),
      static_cast<unsigned>(io.regs.miss_txid_valid_r),
      static_cast<unsigned>(io.regs.miss_txid_r),
      static_cast<unsigned>(io.in.mem_req_accepted),
      static_cast<unsigned>(io.in.mem_req_accepted_id & 0xF),
      static_cast<unsigned>(io.in.mem_resp_valid),
      static_cast<unsigned>(io.in.mem_resp_id & 0xF),
      static_cast<unsigned>(mem_gnt));
  for (uint32_t word = 0; word < ICACHE_LINE_SIZE / 4; ++word) {
    std::printf("%s%08x", (word == 0) ? "" : " ",
                static_cast<unsigned>(line_words[word]));
  }
  std::printf("]\n");
}

inline void dump_icache_module_ctx(const char *tag, long long cycle,
                                   const ICache_IO_t &io, bool mem_gnt) {
  std::printf(
      "[ICACHE][MODULE][%s] cyc=%lld state=%u mem_axi=%u req_valid=%u "
      "lookup_pending=%u meta_resp=%u data_resp=%u data_way=%u "
      "req_pc=0x%08x req_idx=%u ppn_r=0x%05x "
      "in_ppn_v=%u in_ppn=0x%05x mmu_req_v=%u mmu_vtag=0x%05x "
      "mem_req_v=%u mem_req_addr=0x%08x miss_txid_v=%u miss_txid=%u "
      "mem_req_acc=%u mem_req_acc_id=%u mem_resp_v=%u mem_resp_id=%u "
      "mem_gnt=%u page_fault=%u refetch=%u flush=%u\n",
      tag, cycle, static_cast<unsigned>(io.regs.state),
      static_cast<unsigned>(io.regs.mem_axi_state),
      static_cast<unsigned>(io.regs.req_valid_r),
      static_cast<unsigned>(io.regs.lookup_pending_r),
      static_cast<unsigned>(io.lookup_in.meta_resp_valid),
      static_cast<unsigned>(io.lookup_in.data_resp_valid),
      static_cast<unsigned>(io.lookup_in.data_resp_way),
      static_cast<unsigned>(io.regs.req_pc_r),
      static_cast<unsigned>(io.regs.req_index_r),
      static_cast<unsigned>(io.regs.ppn_r),
      static_cast<unsigned>(io.in.ppn_valid),
      static_cast<unsigned>(io.in.ppn),
      static_cast<unsigned>(io.out.mmu_req_valid),
      static_cast<unsigned>(io.out.mmu_req_vtag),
      static_cast<unsigned>(io.out.mem_req_valid),
      static_cast<unsigned>(io.out.mem_req_addr),
      static_cast<unsigned>(io.regs.miss_txid_valid_r),
      static_cast<unsigned>(io.regs.miss_txid_r),
      static_cast<unsigned>(io.in.mem_req_accepted),
      static_cast<unsigned>(io.in.mem_req_accepted_id & 0xF),
      static_cast<unsigned>(io.in.mem_resp_valid),
      static_cast<unsigned>(io.in.mem_resp_id & 0xF),
      static_cast<unsigned>(mem_gnt),
      static_cast<unsigned>(io.in.page_fault),
      static_cast<unsigned>(io.in.refetch),
      static_cast<unsigned>(io.in.flush));
}

inline bool icache_req_slot_idle(const ICache_regs_t &regs) {
  return !regs.req_valid_r &&
         static_cast<ICacheState>(regs.state) == IDLE &&
         static_cast<AXIState>(regs.mem_axi_state) == AXI_IDLE &&
         !regs.miss_txid_valid_r;
}

inline int alloc_free_txid(const ICache_regs_t &regs) {
  for (int id = 0; id < 16; ++id) {
    if (!regs.txid_inflight_r[id]) {
      return id;
    }
  }
  return -1;
}

inline uint32_t icache_line_base_addr(uint32_t ppn, uint32_t index) {
  return (ppn << 12) | (index << ICACHE_OFFSET_BITS);
}
} // namespace

ICache::ICache() {
  reset();

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
  io.regs.miss_ready_seen_r = false;
  io.regs.dcache_probe_inflight_r = false;
  io.regs.dcache_probe_done_r = false;
  io.regs.dcache_probe_hit_r = false;
  io.regs.dcache_probe_word_r = 0;
  for (uint32_t word = 0; word < word_num; ++word) {
    io.regs.dcache_probe_data_r[word] = 0;
  }
  for (int i = 0; i < 16; ++i) {
    io.regs.txid_inflight_r[i] = false;
    io.regs.txid_canceled_r[i] = false;
  }
  perf_state = {};
  perf_state_next = {};
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
  io.regs.miss_ready_seen_r = false;
  io.regs.dcache_probe_inflight_r = false;
  io.regs.dcache_probe_done_r = false;
  io.regs.dcache_probe_hit_r = false;
  io.regs.dcache_probe_word_r = 0;
  for (uint32_t word = 0; word < word_num; ++word) {
    io.regs.dcache_probe_data_r[word] = 0;
  }
  for (int i = 0; i < 16; ++i) {
    io.regs.txid_inflight_r[i] = false;
    io.regs.txid_canceled_r[i] = false;
  }
  perf_state = {};
  perf_state_next = {};
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

void ICache::comb() {
  comb_core(/*allow_lookup_data_phase=*/true);
}

void ICache::comb_lookup_meta() {
  comb_core(/*allow_lookup_data_phase=*/false);
}

void ICache::comb_lookup_data() {
  comb_core(/*allow_lookup_data_phase=*/true);
}

void ICache::comb_core(bool allow_lookup_data_phase) {
  lookup_data_phase_en = allow_lookup_data_phase;
  // Initialize generalized outputs at the start of comb().
  io.out = {};
  perf = {};
  perf_state_next = perf_state;
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
  io.out.ifu_req_ready = req_ready_w;
}

void ICache::capture_lookup_meta_result(uint32_t compare_tag,
                                        bool compare_valid) {
  lookup_hit_valid_w = false;
  lookup_data_ready_w = false;
  lookup_hit_way_w = 0;
  lookup_has_invalid_way_w = false;
  lookup_first_invalid_way_w = 0;
  for (uint32_t word = 0; word < word_num; ++word) {
    lookup_hit_data_w[word] = 0;
  }

  if (!io.lookup_in.meta_resp_valid) {
    return;
  }

  for (uint32_t way = 0; way < ICACHE_WAY_NUM; ++way) {
    const bool valid = io.lookup_in.lookup_set_valid[way];
    if (!valid && !lookup_has_invalid_way_w) {
      lookup_has_invalid_way_w = true;
      lookup_first_invalid_way_w = static_cast<uint8_t>(way);
    }
    if (!compare_valid || lookup_hit_valid_w || !valid) {
      continue;
    }
    if (io.lookup_in.lookup_set_tag[way] != compare_tag) {
      continue;
    }
    lookup_hit_valid_w = true;
    lookup_hit_way_w = static_cast<uint8_t>(way);
  }
}

void ICache::capture_lookup_data_result() {
  lookup_data_ready_w = false;
  for (uint32_t word = 0; word < word_num; ++word) {
    lookup_hit_data_w[word] = 0;
  }

  if (!lookup_data_phase_en || !lookup_hit_valid_w || !io.lookup_in.data_resp_valid ||
      (io.lookup_in.data_resp_way != lookup_hit_way_w)) {
    return;
  }
  lookup_data_ready_w = true;
  for (uint32_t word = 0; word < word_num; ++word) {
    lookup_hit_data_w[word] = io.lookup_in.data_resp_line[word];
  }
}

void ICache::seq() {
  io.regs = io.reg_write;
  perf_state = perf_state_next;
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

  if (kill_pipe) {
    req_ready_w = false;
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

  const bool slot_idle = icache_req_slot_idle(io.regs);
  bool can_accept = io.in.ifu_req_valid && req_ready_w &&
                    !io.regs.lookup_pending_r && slot_idle;
  if (can_accept) {
    if (!use_external_lookup) {
      sram_load_fire = true;
    } else {
      lookup_pending_next = true;
      lookup_index_next = index;
      lookup_pc_next = io.in.pc;
      if (io.lookup_in.meta_resp_valid) {
        sram_load_fire = true;
        lookup_pending_next = false;
      }
    }
  }

  if (use_external_lookup && io.regs.lookup_pending_r &&
      io.lookup_in.meta_resp_valid && slot_idle) {
    sram_load_fire = true;
    lookup_pending_next = false;
  }

  if (sram_load_fire) {
    req_valid_next = true;
    if (SIM_DEBUG_PRINT_ACTIVE && icache_trace_pc(load_pc, sim_time)) {
      std::printf(
          "[ICACHE][MODULE][REQ_LOAD] cyc=%lld load_pc=0x%08x load_idx=%u "
          "slot_idle=%u can_accept=%u lookup_pending_r=%u in_ppn_v=%u "
          "in_ppn=0x%05x mmu_req_v=%u mmu_vtag=0x%05x\n",
          (long long)sim_time, static_cast<unsigned>(load_pc),
          static_cast<unsigned>(load_index),
          static_cast<unsigned>(slot_idle), static_cast<unsigned>(can_accept),
          static_cast<unsigned>(io.regs.lookup_pending_r),
          static_cast<unsigned>(io.in.ppn_valid),
          static_cast<unsigned>(io.in.ppn),
          static_cast<unsigned>(io.out.mmu_req_valid),
          static_cast<unsigned>(io.out.mmu_req_vtag));
    }
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
  io.table_write.invalidate_all = io.in.flush;
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
  io.out.dcache_req_valid = false;
  io.out.dcache_req_addr = 0;
  io.out.ifu_resp_valid = false;
  io.out.lookup_data_req_valid = false;
  io.out.lookup_data_req_index = 0;
  io.out.lookup_data_req_way = 0;
  perf.miss_issue_valid = false;
  perf.miss_penalty_valid = false;
  perf.miss_penalty_cycles = 0;
  perf.axi_read_valid = false;
  perf.axi_read_cycles = 0;
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
  bool same_cycle_accept_resp_candidate =
      io.in.mem_resp_valid && state == SWAP_IN && mem_axi_state == AXI_IDLE &&
      io.regs.miss_txid_valid_r &&
      (resp_id == static_cast<uint8_t>(io.regs.miss_txid_r & 0xF)) &&
      io.in.mem_req_accepted &&
      ((io.in.mem_req_accepted_id & 0xF) ==
       (io.regs.miss_txid_r & 0xF));
  bool active_resp =
      io.in.mem_resp_valid && io.regs.miss_txid_valid_r &&
      (resp_id == io.regs.miss_txid_r) &&
      (io.regs.txid_inflight_r[resp_id] ||
       same_cycle_accept_resp_candidate) &&
      !canceled_resp;
  bool orphan_resp =
      io.in.mem_resp_valid && !active_resp && !canceled_resp &&
      !io.regs.txid_inflight_r[resp_id];

  if (canceled_resp) {
    io.out.mem_resp_ready = true;
    io.reg_write.txid_canceled_r[resp_id] = false;
    io.reg_write.txid_inflight_r[resp_id] = false;
  }
  if (orphan_resp) {
    io.out.mem_resp_ready = true;
    io.reg_write.txid_canceled_r[resp_id] = false;
    io.reg_write.txid_inflight_r[resp_id] = false;
  }

  auto reset_dcache_probe_regs = [&]() {
    io.reg_write.dcache_probe_inflight_r = false;
    io.reg_write.dcache_probe_done_r = false;
    io.reg_write.dcache_probe_hit_r = false;
    io.reg_write.dcache_probe_word_r = 0;
    for (uint32_t word = 0; word < word_num; ++word) {
      io.reg_write.dcache_probe_data_r[word] = 0;
    }
  };

  struct DcacheProbeStep {
    bool done = false;
    bool hit = false;
    uint32_t line[ICACHE_LINE_SIZE / 4] = {0};
  };

  auto step_dcache_probe = [&]() {
    DcacheProbeStep step;
    step.done = io.regs.dcache_probe_done_r;
    step.hit = io.regs.dcache_probe_hit_r;
    for (uint32_t word = 0; word < word_num; ++word) {
      step.line[word] = io.regs.dcache_probe_data_r[word];
    }

    const uint32_t word_idx = io.regs.dcache_probe_word_r;
    if (!io.regs.dcache_probe_done_r &&
        !io.regs.dcache_probe_inflight_r &&
        word_idx < word_num &&
        io.regs.req_valid_r) {
      io.out.dcache_req_valid = true;
      io.out.dcache_req_addr =
          icache_line_base_addr(io.regs.ppn_r, io.regs.req_index_r) +
          (word_idx * 4u);
      // The collaborator ICacheMemPort request has no ready signal; req_valid is
      // treated as an accepted one-cycle request.
      io.reg_write.dcache_probe_inflight_r = true;
    }

    if (io.regs.dcache_probe_inflight_r && io.in.dcache_resp_valid) {
      io.reg_write.dcache_probe_inflight_r = false;
      if (io.in.dcache_resp_miss) {
        step.done = true;
        step.hit = false;
        io.reg_write.dcache_probe_done_r = true;
        io.reg_write.dcache_probe_hit_r = false;
      } else {
        step.hit = true;
        step.line[word_idx] = io.in.dcache_resp_data;
        io.reg_write.dcache_probe_hit_r = true;
        io.reg_write.dcache_probe_data_r[word_idx] = io.in.dcache_resp_data;
        if (word_idx + 1u >= word_num) {
          step.done = true;
          io.reg_write.dcache_probe_done_r = true;
        } else {
          io.reg_write.dcache_probe_word_r = word_idx + 1u;
        }
      }
    }
    return step;
  };

  auto latch_mem_refill = [&](bool same_cycle_issue) {
    mem_gnt = true;
    if (perf_state.miss_penalty_active &&
        static_cast<uint64_t>(sim_time) >=
            perf_state.miss_penalty_start_cycle) {
      perf.miss_penalty_valid = true;
      perf.miss_penalty_cycles =
          static_cast<uint64_t>(sim_time) -
          perf_state.miss_penalty_start_cycle;
    }
    if (same_cycle_issue) {
      perf.axi_read_valid = true;
      perf.axi_read_cycles = 0;
    } else if (perf_state.axi_read_active &&
               static_cast<uint64_t>(sim_time) >=
                   perf_state.axi_read_start_cycle) {
      perf.axi_read_valid = true;
      perf.axi_read_cycles =
          static_cast<uint64_t>(sim_time) - perf_state.axi_read_start_cycle;
    }
    for (uint32_t offset = 0; offset < word_num; ++offset) {
      mem_resp_data_w[offset] = io.in.mem_resp_data[offset];
    }
    capture_lookup_meta_result(io.regs.ppn_r & 0xFFFFF,
                               /*compare_valid=*/false);
    if (lookup_has_invalid_way_w) {
      replace_idx_next = lookup_first_invalid_way_w;
    } else {
      replace_idx_next = (io.regs.replace_idx + 1) % way_cnt;
    }
    perf_state_next.miss_penalty_active = false;
    perf_state_next.miss_penalty_start_cycle = 0;
    perf_state_next.axi_read_active = false;
    perf_state_next.axi_read_start_cycle = 0;
  };

  auto finish_refill = [&](const uint32_t *line_words, uint32_t replace_way,
                           const char *debug_tag) {
    state_next = IDLE;
    mem_axi_state_next = AXI_IDLE;
    if (io.regs.miss_txid_valid_r) {
      io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = false;
    }
    io.reg_write.miss_txid_valid_r = false;
    io.reg_write.miss_ready_seen_r = false;
    reset_dcache_probe_regs();
    perf_state_next.miss_penalty_active = false;
    perf_state_next.miss_penalty_start_cycle = 0;
    perf_state_next.axi_read_active = false;
    perf_state_next.axi_read_start_cycle = 0;

    if (!io.in.flush) {
      io.table_write.we = true;
      io.table_write.index = io.regs.req_index_r;
      io.table_write.way = replace_way;
      for (uint32_t word = 0; word < word_num; ++word) {
        io.table_write.data[word] = line_words[word];
      }
      io.table_write.tag = io.regs.ppn_r;
      io.table_write.valid = true;
    }

    if (!io.in.refetch && !io.in.flush) {
      io.out.ifu_resp_valid = true;
      for (uint32_t word = 0; word < word_num; ++word) {
        io.out.rd_data[word] = line_words[word];
      }
      if (SIM_DEBUG_PRINT_ACTIVE &&
          icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
        dump_icache_module_line(debug_tag, sim_time, io, mem_gnt,
                                io.out.rd_data);
      }
    }
    req_ready_w = true;
  };

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
        if (SIM_DEBUG_PRINT_ACTIVE &&
            icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
          dump_icache_module_ctx("FAST_PF_BYPASS", sim_time, io, mem_gnt);
        }
        req_ready_w = true;
        fast_bypass_fire = true;
        state_next = IDLE;
        break;
      }

      uint32_t index = (io.in.pc >> offset_bits) & (set_num - 1u);
      capture_lookup_meta_result(io.in.ppn & 0xFFFFF, /*compare_valid=*/true);
      capture_lookup_data_result();
      if (lookup_hit_valid_w && lookup_data_ready_w) {
        for (uint32_t word = 0; word < word_num; ++word) {
          io.out.rd_data[word] = lookup_hit_data_w[word];
        }
        io.out.ifu_resp_valid = true;
        io.out.ifu_page_fault = false;
        if (SIM_DEBUG_PRINT_ACTIVE &&
            icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
          dump_icache_module_line("FAST_HIT_BYPASS", sim_time, io, mem_gnt,
                                  io.out.rd_data);
        }
        req_ready_w = true;
        fast_bypass_fire = true;
        state_next = IDLE;
        break;
      } else if (lookup_hit_valid_w) {
        io.out.lookup_data_req_valid = true;
        io.out.lookup_data_req_index = index;
        io.out.lookup_data_req_way = lookup_hit_way_w;
        req_ready_w = false;
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
        if (SIM_DEBUG_PRINT_ACTIVE &&
            icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
          dump_icache_module_ctx("REG_PF", sim_time, io, mem_gnt);
        }
        req_ready_w = true;
        state_next = IDLE;
        break;
      }

      if (SIM_DEBUG_PRINT_ACTIVE &&
          icache_trace_pc(io.regs.req_pc_r, sim_time) &&
          !io.lookup_in.meta_resp_valid) {
        dump_icache_module_ctx("REG_LOOKUP_STALE", sim_time, io, mem_gnt);
      }

      capture_lookup_meta_result(io.in.ppn & 0xFFFFF, /*compare_valid=*/true);
      capture_lookup_data_result();
      io.out.ifu_resp_valid = lookup_hit_valid_w && lookup_data_ready_w;
      req_ready_w = lookup_hit_valid_w && lookup_data_ready_w;
      if (lookup_hit_valid_w && lookup_data_ready_w) {
        for (uint32_t word = 0; word < word_num; ++word) {
          io.out.rd_data[word] = lookup_hit_data_w[word];
        }
        if (SIM_DEBUG_PRINT_ACTIVE &&
            icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
          dump_icache_module_line("REG_HIT", sim_time, io, mem_gnt,
                                  io.out.rd_data);
        }
        state_next = IDLE;
      } else if (lookup_hit_valid_w) {
        io.out.lookup_data_req_valid = true;
        io.out.lookup_data_req_index = io.regs.req_index_r;
        io.out.lookup_data_req_way = lookup_hit_way_w;
        state_next = IDLE;
      } else {
        if (SIM_DEBUG_PRINT_ACTIVE &&
            icache_trace_pc(io.out.ifu_resp_pc, sim_time)) {
          dump_icache_module_ctx("REG_MISS", sim_time, io, mem_gnt);
          std::printf(
              "[ICACHE][MODULE][REG_MISS_SET] cyc=%lld req_pc=0x%08x req_idx=%u "
              "cmp_ppn=0x%05x hit_v=%u has_inv=%u first_inv=%u",
              sim_time, static_cast<unsigned>(io.regs.req_pc_r),
              static_cast<unsigned>(io.regs.req_index_r),
              static_cast<unsigned>(io.in.ppn & 0xFFFFF),
              static_cast<unsigned>(lookup_hit_valid_w),
              static_cast<unsigned>(lookup_has_invalid_way_w),
              static_cast<unsigned>(lookup_first_invalid_way_w));
          std::printf("\n");
        }
        int txid = io.regs.miss_txid_valid_r ? static_cast<int>(io.regs.miss_txid_r)
                                             : alloc_free_txid(io.regs);
        if (txid < 0) {
          state_next = IDLE;
          req_ready_w = false;
        } else {
          // A miss carries the translated PPN as part of the refill request
          // context. Latching it here avoids issuing the next-cycle AXI read
          // with the previous request's stale ppn_r under redirect pressure.
          io.reg_write.ppn_r = io.in.ppn;
          io.reg_write.dcache_probe_inflight_r = false;
          io.reg_write.dcache_probe_done_r = false;
          io.reg_write.dcache_probe_hit_r = false;
          io.reg_write.dcache_probe_word_r = 0;
          for (uint32_t word = 0; word < word_num; ++word) {
            io.reg_write.dcache_probe_data_r[word] = 0;
          }
          io.reg_write.miss_txid_valid_r = true;
          io.reg_write.miss_txid_r = static_cast<uint8_t>(txid & 0xF);
          io.reg_write.miss_ready_seen_r = false;
          perf_state_next.miss_penalty_active = true;
          perf_state_next.miss_penalty_start_cycle =
              static_cast<uint64_t>(sim_time);
          perf_state_next.axi_read_active = false;
          perf_state_next.axi_read_start_cycle = 0;
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
    {
    DcacheProbeStep dcache_step = step_dcache_probe();
    const bool req_ready_seen_now =
        (mem_axi_state == AXI_IDLE) && io.in.mem_req_ready &&
        io.regs.miss_txid_valid_r;
    if (req_ready_seen_now) {
      io.reg_write.miss_ready_seen_r = true;
    }
    const bool req_accepted_now =
        (mem_axi_state == AXI_IDLE) && io.in.mem_req_accepted &&
        ((io.in.mem_req_accepted_id & 0xF) == (io.regs.miss_txid_r & 0xF)) &&
        io.regs.miss_txid_valid_r;
    const bool req_issue_now =
        io.regs.miss_txid_valid_r &&
        !io.regs.miss_ready_seen_r &&
        (req_accepted_now || req_ready_seen_now);
    if (req_issue_now) {
      perf.miss_issue_valid = true;
    }
    // Under the ready-first interconnect contract, a miss request may already
    // be committed downstream once req.ready is observed, even if the explicit
    // accepted pulse arrives a cycle later. On a refetch/redirect, dropping the
    // miss state too early allows the same txid to be reused before the stale
    // response returns.
    const bool req_may_complete_later =
        io.regs.miss_txid_valid_r &&
        (mem_axi_state != AXI_IDLE || req_accepted_now ||
         io.regs.miss_ready_seen_r || req_ready_seen_now);
    if (kill_pipe) {
      const bool req_already_inflight =
          io.regs.miss_txid_valid_r &&
          (mem_axi_state != AXI_IDLE || req_accepted_now);
      const bool req_waiting_accept =
          io.regs.miss_txid_valid_r && !req_already_inflight &&
          (io.regs.miss_ready_seen_r || req_ready_seen_now);
      if (req_already_inflight) {
        uint8_t txid = io.regs.miss_txid_r & 0xF;
        io.reg_write.txid_canceled_r[txid] = true;
        io.reg_write.txid_inflight_r[txid] = true;
        state_next = IDLE;
        mem_axi_state_next = AXI_IDLE;
        io.reg_write.miss_txid_valid_r = false;
        io.reg_write.miss_ready_seen_r = false;
        reset_dcache_probe_regs();
        perf_state_next.miss_penalty_active = false;
        perf_state_next.miss_penalty_start_cycle = 0;
        perf_state_next.axi_read_active = false;
        perf_state_next.axi_read_start_cycle = 0;
        req_ready_w = true;
      } else if (req_waiting_accept) {
        state_next = CANCEL_WAIT_ACCEPT;
        mem_axi_state_next = AXI_IDLE;
        reset_dcache_probe_regs();
        req_ready_w = false;
      } else {
        (void)req_may_complete_later;
        state_next = IDLE;
        mem_axi_state_next = AXI_IDLE;
        io.reg_write.miss_txid_valid_r = false;
        io.reg_write.miss_ready_seen_r = false;
        reset_dcache_probe_regs();
        perf_state_next.miss_penalty_active = false;
        perf_state_next.miss_penalty_start_cycle = 0;
        perf_state_next.axi_read_active = false;
        perf_state_next.axi_read_start_cycle = 0;
        req_ready_w = true;
      }
      break;
    }

    if (mem_axi_state == AXI_IDLE) {
      io.out.mem_req_addr =
          (io.regs.ppn_r << 12) | (io.regs.req_index_r << offset_bits);
      io.out.mem_req_id = io.regs.miss_txid_r;
      if (SIM_DEBUG_PRINT_ACTIVE &&
          icache_trace_pc(io.regs.req_pc_r, sim_time)) {
        dump_icache_module_ctx("SWAP_REQ", sim_time, io, mem_gnt);
      }
      state_next = SWAP_IN;
      if (io.in.mem_req_accepted &&
          ((io.in.mem_req_accepted_id & 0xF) == (io.regs.miss_txid_r & 0xF))) {
        io.out.mem_req_valid = false;
        if (active_resp) {
          // Extra AXI/LLC sub-ticks can complete the refill before the next
          // CPU cycle observes the registered inflight bit.
          io.out.mem_resp_ready = true;
          latch_mem_refill(/*same_cycle_issue=*/true);
          DcacheProbeStep dcache_step = step_dcache_probe();
          if (!dcache_step.done) {
            state_next = WAIT_DCACHE_AFTER_MEM;
            mem_axi_state_next = AXI_IDLE;
            io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = false;
            io.reg_write.miss_txid_valid_r = false;
            io.reg_write.miss_ready_seen_r = false;
            req_ready_w = false;
          } else {
            finish_refill(dcache_step.hit ? dcache_step.line : mem_resp_data_w,
                          replace_idx_next,
                          dcache_step.hit ? "SWAP_DCACHE_HIT_SAME_CYCLE"
                                          : "SWAP_MEM_GNT_SAME_CYCLE");
          }
        } else {
          mem_axi_state_next = AXI_BUSY;
          io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = true;
          perf_state_next.axi_read_active = true;
          perf_state_next.axi_read_start_cycle =
              static_cast<uint64_t>(sim_time);
        }
      } else {
        io.out.mem_req_valid = true;
        mem_axi_state_next = AXI_IDLE;
      }
    } else {
      io.out.mem_req_valid = false;
      io.out.mem_resp_ready = io.out.mem_resp_ready || active_resp;

      mem_gnt = active_resp && io.out.mem_resp_ready;
      state_next = SWAP_IN;
      if (mem_gnt) {
        latch_mem_refill(/*same_cycle_issue=*/false);

        if (!dcache_step.done) {
          state_next = WAIT_DCACHE_AFTER_MEM;
          mem_axi_state_next = AXI_IDLE;
          io.reg_write.txid_inflight_r[io.regs.miss_txid_r & 0xF] = false;
          io.reg_write.miss_txid_valid_r = false;
          io.reg_write.miss_ready_seen_r = false;
          req_ready_w = false;
          break;
        }
        finish_refill(dcache_step.hit ? dcache_step.line : mem_resp_data_w,
                      replace_idx_next,
                      dcache_step.hit ? "SWAP_DCACHE_HIT" : "SWAP_MEM_GNT");
      }
    }
    break;
    }

  case WAIT_DCACHE_AFTER_MEM:
    if (kill_pipe) {
      state_next = IDLE;
      mem_axi_state_next = AXI_IDLE;
      io.reg_write.miss_txid_valid_r = false;
      io.reg_write.miss_ready_seen_r = false;
      reset_dcache_probe_regs();
      perf_state_next.miss_penalty_active = false;
      perf_state_next.miss_penalty_start_cycle = 0;
      perf_state_next.axi_read_active = false;
      perf_state_next.axi_read_start_cycle = 0;
      req_ready_w = true;
      break;
    }
    {
      DcacheProbeStep dcache_step = step_dcache_probe();
      if (!dcache_step.done) {
        state_next = WAIT_DCACHE_AFTER_MEM;
        req_ready_w = false;
        break;
      }
      if (perf_state.miss_penalty_active &&
          static_cast<uint64_t>(sim_time) >= perf_state.miss_penalty_start_cycle) {
        perf.miss_penalty_valid = true;
        perf.miss_penalty_cycles =
            static_cast<uint64_t>(sim_time) - perf_state.miss_penalty_start_cycle;
      }
      finish_refill(dcache_step.hit ? dcache_step.line
                                    : io.regs.mem_resp_data_r,
                    io.regs.replace_idx,
                    dcache_step.hit ? "DCACHE_HIT_AFTER_MEM"
                                    : "MEM_GNT_AFTER_DCACHE_MISS");
    }
    break;

  case CANCEL_WAIT_ACCEPT:
    io.out.mem_req_valid = false;
    io.out.mem_resp_ready = true;
    if (io.in.mem_req_accepted && io.regs.miss_txid_valid_r &&
        ((io.in.mem_req_accepted_id & 0xF) == (io.regs.miss_txid_r & 0xF))) {
      uint8_t txid = io.regs.miss_txid_r & 0xF;
      io.reg_write.txid_canceled_r[txid] = true;
      io.reg_write.txid_inflight_r[txid] = true;
    }
    state_next = IDLE;
    mem_axi_state_next = AXI_IDLE;
    io.reg_write.miss_txid_valid_r = false;
    io.reg_write.miss_ready_seen_r = false;
    reset_dcache_probe_regs();
    perf_state_next.miss_penalty_active = false;
    perf_state_next.miss_penalty_start_cycle = 0;
    perf_state_next.axi_read_active = false;
    perf_state_next.axi_read_start_cycle = 0;
    req_ready_w = true;
    break;

  case DRAIN:
    if (kill_pipe && !mem_wait_must_drain_on_refetch()) {
      state_next = IDLE;
      mem_axi_state_next = AXI_IDLE;
      io.reg_write.miss_txid_valid_r = false;
      io.reg_write.miss_ready_seen_r = false;
      reset_dcache_probe_regs();
      perf_state_next.miss_penalty_active = false;
      perf_state_next.miss_penalty_start_cycle = 0;
      perf_state_next.axi_read_active = false;
      perf_state_next.axi_read_start_cycle = 0;
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
      io.reg_write.miss_ready_seen_r = false;
      reset_dcache_probe_regs();
      perf_state_next.miss_penalty_active = false;
      perf_state_next.miss_penalty_start_cycle = 0;
      perf_state_next.axi_read_active = false;
      perf_state_next.axi_read_start_cycle = 0;
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
  case WAIT_DCACHE_AFTER_MEM:
    std::cout << "WAIT_DCACHE_AFTER_MEM";
    break;
  case CANCEL_WAIT_ACCEPT:
    std::cout << "CANCEL_WAIT_ACCEPT";
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
  case WAIT_DCACHE_AFTER_MEM:
    std::cout << "WAIT_DCACHE_AFTER_MEM";
    break;
  case CANCEL_WAIT_ACCEPT:
    std::cout << "CANCEL_WAIT_ACCEPT";
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
  (void)index;
  std::cout << "Cache contents are owned by the external lookup tables."
            << std::endl;
}
void ICache::log_valid(uint32_t index) {
  (void)index;
  std::cout << "Valid bits are owned by the external lookup tables."
            << std::endl;
}
void ICache::log_pipeline() {
  std::cout << "Request Context:" << std::endl;
  std::cout << "  req_valid_r: " << io.regs.req_valid_r << std::endl;
  std::cout << "  req_index_r: " << io.regs.req_index_r << std::endl;
  std::cout << "  ppn_r: 0x" << std::hex << io.regs.ppn_r << std::dec << std::endl;
  std::cout << "  ifu_req_ready_r: " << io.regs.ifu_req_ready_r << std::endl;
}
