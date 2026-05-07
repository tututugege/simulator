#include "RealLsuPerf.h"
#include "types.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace lsu_perf {
namespace {

#if !BSD_CONFIG
int load_state_index(LoadState state) {
  const int index = static_cast<int>(state);
  if (index >= 0 && index < PerfCount::kLdqStateOtherIndex) {
    return index;
  }
  return PerfCount::kLdqStateOtherIndex;
}

int store_state_index(StoreState state) {
  const int index = static_cast<int>(state);
  if (index >= 0 && index < PerfCount::kStqStateOtherIndex) {
    return index;
  }
  return PerfCount::kStqStateOtherIndex;
}

void sample_lsu_state_occupancy(PerfCount &perf, const LsuState &state) {
  std::array<uint64_t, PerfCount::kLdqStatePerfCount> ldq_state_count = {};
  for (int i = 0; i < LDQ_SIZE; i++) {
    ldq_state_count[load_state_index(state.ldq[i].load_state)]++;
  }

  std::array<uint64_t, PerfCount::kStqStatePerfCount> stq_state_count = {};
  const uint32_t active_stq_count =
      std::min<uint32_t>(static_cast<uint32_t>(state.stq_count), STQ_SIZE);
  stq_state_count[static_cast<int>(StoreState::Empty)] =
      STQ_SIZE - active_stq_count;
  for (uint32_t i = 0; i < active_stq_count; i++) {
    const uint32_t stq_idx = (state.stq_head + i) % STQ_SIZE;
    stq_state_count[store_state_index(state.stq[stq_idx].store_state)]++;
  }

  for (int i = 0; i < PerfCount::kLdqStatePerfCount; i++) {
    perf.ldq_state_average_count[i] += ldq_state_count[i];
    if (ldq_state_count[i] > perf.ldq_state_max_count[i]) {
      perf.ldq_state_max_count[i] = ldq_state_count[i];
    }
  }
  for (int i = 0; i < PerfCount::kStqStatePerfCount; i++) {
    perf.stq_state_average_count[i] += stq_state_count[i];
    if (stq_state_count[i] > perf.stq_state_max_count[i]) {
      perf.stq_state_max_count[i] = stq_state_count[i];
    }
  }
}

void count_stq_state(PerfCount &perf, StoreState state, bool head_sample) {
  uint64_t *counter = nullptr;
  switch (state) {
  case StoreState::Committed:
    counter = head_sample ? &perf.stq_diag_head_committed
                          : &perf.stq_diag_window_committed;
    break;
  case StoreState::Done:
    counter = head_sample ? &perf.stq_diag_head_done
                          : &perf.stq_diag_window_done;
    break;
  case StoreState::WaitDcacheResp:
    counter = head_sample ? &perf.stq_diag_head_wait_dcache
                          : &perf.stq_diag_window_wait_dcache;
    break;
  case StoreState::WaitMmioResp:
    counter = head_sample ? &perf.stq_diag_head_wait_mmio
                          : &perf.stq_diag_window_wait_mmio;
    break;
  case StoreState::WaitTlb:
    counter = head_sample ? &perf.stq_diag_head_wait_tlb
                          : &perf.stq_diag_window_wait_tlb;
    break;
  case StoreState::WaitData:
    counter = head_sample ? &perf.stq_diag_head_wait_data
                          : &perf.stq_diag_window_wait_data;
    break;
  case StoreState::PageFault:
    counter = head_sample ? &perf.stq_diag_head_page_fault
                          : &perf.stq_diag_window_page_fault;
    break;
  default:
    counter = head_sample ? &perf.stq_diag_head_other
                          : &perf.stq_diag_window_other;
    break;
  }
  (*counter)++;
}

void count_stq_same_addr_older(PerfCount &perf, StoreState state) {
  switch (state) {
  case StoreState::Committed:
    perf.stq_same_addr_block_older_committed++;
    break;
  case StoreState::WaitDcacheResp:
    perf.stq_same_addr_block_older_wait_dcache++;
    break;
  case StoreState::WaitMmioResp:
    perf.stq_same_addr_block_older_wait_mmio++;
    break;
  case StoreState::WaitTlb:
    perf.stq_same_addr_block_older_wait_tlb++;
    break;
  case StoreState::WaitData:
    perf.stq_same_addr_block_older_wait_data++;
    break;
  case StoreState::PageFault:
    perf.stq_same_addr_block_older_page_fault++;
    break;
  default:
    perf.stq_same_addr_block_older_other++;
    break;
  }
}

void count_stq_replay_resp(PerfCount &perf, ReplayType replay) {
  perf.stq_dcache_resp_replay++;
  switch (replay) {
  case ReplayType::CONFLICT:
    perf.stq_dcache_resp_replay_conflict++;
    break;
  case ReplayType::MSHR_HIT:
    perf.stq_dcache_resp_replay_mshr_hit++;
    break;
  case ReplayType::MSHR_FULL:
    perf.stq_dcache_resp_replay_mshr_full++;
    break;
  case ReplayType::HIT:
    break;
  }
}

void record_mem_inst_latency(SimContext *ctx, bool started,
                             uint64_t start_cycle) {
  if (ctx == nullptr || !started) {
    return;
  }
  const uint64_t now = ctx->perf.cycle;
  if (now >= start_cycle) {
    ctx->perf.l1d_mem_inst_total_cycles += now - start_cycle;
    ctx->perf.l1d_mem_inst_samples++;
  }
}
#endif

} // namespace

uint64_t load_miss_mask(const LsuState &state) {
  uint64_t miss_mask = 0;
#if !BSD_CONFIG
  for (int i = 0; i < LDQ_SIZE; i++) {
    if (state.ldq[i].cache_miss == true &&
        state.ldq[i].load_state != LoadState::Empty) {
      miss_mask |= (1ULL << state.ldq[i].rob_idx);
    }
  }
#else
  (void)state;
#endif
  return miss_mask;
}

void sample_queue_occupancy(SimContext *ctx, const LsuState &state,
                            uint32_t ldq_count) {
#if !BSD_CONFIG
  if (ctx == nullptr) {
    return;
  }
  ctx->perf.ldq_average_count += static_cast<uint64_t>(ldq_count);
  ctx->perf.stq_commit_average_count +=
      static_cast<uint64_t>(state.stq_commit_count);
  ctx->perf.stq_average_count += static_cast<uint64_t>(state.stq_count);
  ctx->perf.wait_mmu_stq_average_count +=
      static_cast<uint64_t>(state.wait_mmu_stq_count);
  ctx->perf.wait_mmu_ldq_average_count +=
      static_cast<uint64_t>(state.wait_mmu_ldq_count);
  ctx->perf.mmu_done_stq_average_count +=
      static_cast<uint64_t>(state.mmu_done_stq_count);
  ctx->perf.finish_average_count += static_cast<uint64_t>(state.finish_count);
  ctx->perf.stlf_queue_average_count +=
      static_cast<uint64_t>(state.stlf_queue_count);
  ctx->perf.wait_dcache_ldq_average_count +=
      static_cast<uint64_t>(state.wait_dcache_ldq_count);
  sample_lsu_state_occupancy(ctx->perf, state);
  if (static_cast<uint64_t>(ldq_count) > ctx->perf.ldq_max_count) {
    ctx->perf.ldq_max_count = static_cast<uint64_t>(ldq_count);
  }
  if (static_cast<uint64_t>(state.stq_count) > ctx->perf.stq_max_count) {
    ctx->perf.stq_max_count = static_cast<uint64_t>(state.stq_count);
  }
  if (static_cast<uint64_t>(state.stq_commit_count) >
      ctx->perf.stq_commit_max_count) {
    ctx->perf.stq_commit_max_count =
        static_cast<uint64_t>(state.stq_commit_count);
  }
  if (static_cast<uint64_t>(state.wait_mmu_stq_count) >
      ctx->perf.wait_mmu_stq_max_count) {
    ctx->perf.wait_mmu_stq_max_count =
        static_cast<uint64_t>(state.wait_mmu_stq_count);
  }
  if (static_cast<uint64_t>(state.wait_mmu_ldq_count) >
      ctx->perf.wait_mmu_ldq_max_count) {
    ctx->perf.wait_mmu_ldq_max_count =
        static_cast<uint64_t>(state.wait_mmu_ldq_count);
  }
  if (static_cast<uint64_t>(state.mmu_done_stq_count) >
      ctx->perf.mmu_done_stq_max_count) {
    ctx->perf.mmu_done_stq_max_count =
        static_cast<uint64_t>(state.mmu_done_stq_count);
  }
  if (static_cast<uint64_t>(state.finish_count) >
      ctx->perf.finish_max_count) {
    ctx->perf.finish_max_count = static_cast<uint64_t>(state.finish_count);
  }
  if (static_cast<uint64_t>(state.stlf_queue_count) >
      ctx->perf.stlf_queue_max_count) {
    ctx->perf.stlf_queue_max_count =
        static_cast<uint64_t>(state.stlf_queue_count);
  }
  if (static_cast<uint64_t>(state.wait_dcache_ldq_count) >
      ctx->perf.wait_dcache_ldq_max_count) {
    ctx->perf.wait_dcache_ldq_max_count =
        static_cast<uint64_t>(state.wait_dcache_ldq_count);
  }
#else
  (void)ctx;
  (void)state;
  (void)ldq_count;
#endif
}

void finish_mem_inst(SimContext *ctx, LdqEntry &entry) {
#if !BSD_CONFIG
  record_mem_inst_latency(ctx, entry.perf_mem_started,
                          entry.perf_mem_start_cycle);
  entry.perf_mem_started = false;
#else
  (void)ctx;
  (void)entry;
#endif
}

void finish_mem_inst(SimContext *ctx, StqEntry &entry) {
#if !BSD_CONFIG
  record_mem_inst_latency(ctx, entry.perf_mem_started,
                          entry.perf_mem_start_cycle);
  entry.perf_mem_started = false;
#else
  (void)ctx;
  (void)entry;
#endif
}

void start_mem_inst(SimContext *ctx, LdqEntry &entry) {
#if !BSD_CONFIG
  entry.perf_mem_started = true;
  entry.perf_mem_start_cycle = ctx == nullptr ? 0 : ctx->perf.cycle;
#else
  (void)ctx;
  (void)entry;
#endif
}

void start_mem_inst(SimContext *ctx, StqEntry &entry) {
#if !BSD_CONFIG
  entry.perf_mem_started = true;
  entry.perf_mem_start_cycle = ctx == nullptr ? 0 : ctx->perf.cycle;
#else
  (void)ctx;
  (void)entry;
#endif
}

void start_mem_inst_if_needed(SimContext *ctx, LdqEntry &entry) {
#if !BSD_CONFIG
  if (!entry.perf_mem_started) {
    start_mem_inst(ctx, entry);
  }
#else
  (void)ctx;
  (void)entry;
#endif
}

void start_mem_inst_if_needed(SimContext *ctx, const StqEntry &cur_entry,
                              StqEntry &nxt_entry) {
#if !BSD_CONFIG
  if (!cur_entry.perf_mem_started) {
    start_mem_inst(ctx, nxt_entry);
  }
#else
  (void)ctx;
  (void)cur_entry;
  (void)nxt_entry;
#endif
}

void reset_wait_start(WaitDcacheLDQEntry &entry) {
#if !BSD_CONFIG
  entry.wait_start_cycle = 0;
#else
  (void)entry;
#endif
}

void mark_wait_start(SimContext *ctx, WaitDcacheLDQEntry &entry) {
#if !BSD_CONFIG
  entry.wait_start_cycle = ctx == nullptr ? 0 : ctx->perf.cycle;
#else
  (void)ctx;
  (void)entry;
#endif
}

void retry_load_resp_timeouts(SimContext *ctx, const LsuState &cur,
                              LsuState &nxt) {
#if !BSD_CONFIG && CONFIG_LSU_DRESP_TIMEOUT_CYCLES > 0
  for (int i = 0; i < cur.wait_dcache_ldq_count; i++) {
    const uint32_t wait_idx = (cur.wait_dcache_ldq_head + i) % LDQ_SIZE;
    const auto &entry = cur.wait_dcache_ldq[wait_idx];
    if (!entry.valid) {
      continue;
    }
    const LdqEntry &ldq_entry = cur.ldq[entry.ldq_idx];
    if (ldq_entry.load_state != LoadState::WaitDcacheResp ||
        entry.wait_start_cycle == 0 || ctx == nullptr) {
      continue;
    }
    if (ctx->perf.cycle - entry.wait_start_cycle >=
        static_cast<uint64_t>(CONFIG_LSU_DRESP_TIMEOUT_CYCLES)) {
      nxt.ldq[entry.ldq_idx].load_state = LoadState::ReadyToIssue;
      nxt.ldq[entry.ldq_idx].replay_type = ReplayType::CONFLICT;
      nxt.wait_dcache_ldq[wait_idx].wait_start_cycle = 0;
      ctx->perf.ld_resp_timeout_retry_count++;
    }
  }
#else
  (void)ctx;
  (void)cur;
  (void)nxt;
#endif
}

void clear_replay(LoadReq &req) {
#if !BSD_CONFIG
  req.replay = false;
#else
  (void)req;
#endif
}

void clear_replay(StoreReq &req) {
#if !BSD_CONFIG
  req.replay = false;
#else
  (void)req;
#endif
}

void set_replay(LoadReq &req, bool is_replay) {
#if !BSD_CONFIG
  req.replay = is_replay;
#else
  (void)req;
  (void)is_replay;
#endif
}

void set_replay(StoreReq &req, bool is_replay) {
#if !BSD_CONFIG
  req.replay = is_replay;
#else
  (void)req;
  (void)is_replay;
#endif
}

bool is_load_replay(const LdqEntry &entry) {
#if !BSD_CONFIG
  return entry.replay_type != ReplayType::HIT;
#else
  (void)entry;
  return false;
#endif
}

bool is_store_replay(const StqEntry &entry) {
#if !BSD_CONFIG
  return entry.replay_type != ReplayType::HIT;
#else
  (void)entry;
  return false;
#endif
}

void mark_load_hit(LdqEntry &entry) {
#if !BSD_CONFIG
  entry.replay_type = ReplayType::HIT;
#else
  (void)entry;
#endif
}

void mark_load_replay(LdqEntry &entry, ReplayType replay) {
#if !BSD_CONFIG
  entry.replay_type = replay;
  if (entry.cache_miss == false) {
    entry.cache_miss = true;
  }
#else
  (void)entry;
  (void)replay;
#endif
}

void mark_store_hit(StqEntry &entry) {
#if !BSD_CONFIG
  entry.replay_type = ReplayType::HIT;
#else
  (void)entry;
#endif
}

void mark_store_replay(StqEntry &entry, ReplayType replay) {
#if !BSD_CONFIG
  entry.replay_type = replay;
#else
  (void)entry;
  (void)replay;
#endif
}

void count_stlf_waiting_on_unknown_store(SimContext *ctx,
                                         const LsuState &state,
                                         uint32_t older_store_count) {
#if !BSD_CONFIG
  if (ctx == nullptr) {
    return;
  }
  ctx->perf.ld_stlf_check_count++;
  for (uint32_t j = 0; j < older_store_count; j++) {
    const uint32_t stq_idx = (state.stq_head + j) % STQ_SIZE;
    if (!state.stq[stq_idx].paddr_valid) {
      ctx->perf.ld_stlf_block_unknown_store_addr_count++;
      break;
    }
  }
#else
  (void)ctx;
  (void)state;
  (void)older_store_count;
#endif
}

void count_stlf_unknown_store_block(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.ld_stlf_check_count++;
    ctx->perf.ld_stlf_block_unknown_store_addr_count++;
  }
#else
  (void)ctx;
#endif
}

void count_stlf_check(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.ld_stlf_check_count++;
  }
#else
  (void)ctx;
#endif
}

void count_mmio_load_issue(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.mmio_inst_count++;
    ctx->perf.mmio_load_count++;
  }
#else
  (void)ctx;
#endif
}

void count_mmio_store_issue(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.mmio_inst_count++;
    ctx->perf.mmio_store_count++;
  }
#else
  (void)ctx;
#endif
}

void count_mmio_head_block(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.mmio_head_block_cycles++;
  }
#else
  (void)ctx;
#endif
}

void count_load_dcache_issue(SimContext *ctx, bool is_replay) {
#if !BSD_CONFIG
  if (ctx == nullptr) {
    return;
  }
  ctx->perf.l1d_req_all++;
  if (is_replay) {
    ctx->perf.l1d_req_replay++;
  } else {
    ctx->perf.l1d_req_initial++;
    ctx->perf.dcache_access_num++;
  }
#else
  (void)ctx;
  (void)is_replay;
#endif
}

void count_stale_load_resp(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.ld_resp_stale_drop_count++;
  }
#else
  (void)ctx;
#endif
}

void count_store_dcache_hit(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.stq_dcache_resp_hit++;
  }
#else
  (void)ctx;
#endif
}

void count_store_replay_resp(SimContext *ctx, ReplayType replay) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    count_stq_replay_resp(ctx->perf, replay);
  }
#else
  (void)ctx;
  (void)replay;
#endif
}

void sample_stq_issue_window(SimContext *ctx, const LsuState &state,
                             uint32_t commit_count) {
#if !BSD_CONFIG
  if (ctx == nullptr || commit_count == 0) {
    return;
  }
  ctx->perf.stq_diag_window_cycles++;
  ctx->perf.stq_diag_window_entries += commit_count;
  ctx->perf.stq_diag_head_samples++;
  count_stq_state(ctx->perf, state.stq[state.stq_head].store_state, true);
  for (uint32_t i = 0; i < commit_count; i++) {
    const uint32_t stq_idx = (state.stq_head + i) % STQ_SIZE;
    count_stq_state(ctx->perf, state.stq[stq_idx].store_state, false);
  }
#else
  (void)ctx;
  (void)state;
  (void)commit_count;
#endif
}

void count_same_addr_block(SimContext *ctx, StoreState older_state,
                           uint32_t distance) {
#if !BSD_CONFIG
  if (ctx == nullptr) {
    return;
  }
  ctx->perf.stq_same_addr_block_count++;
  count_stq_same_addr_older(ctx->perf, older_state);
  ctx->perf.stq_same_addr_block_distance_sum += distance;
  if (distance > ctx->perf.stq_same_addr_block_distance_max) {
    ctx->perf.stq_same_addr_block_distance_max = distance;
  }
#else
  (void)ctx;
  (void)older_state;
  (void)distance;
#endif
}

void count_suppress_store_done(SimContext *ctx) {
#if !BSD_CONFIG
  if (ctx != nullptr) {
    ctx->perf.stq_diag_suppress_done++;
  }
#else
  (void)ctx;
#endif
}

void count_store_dcache_issue(SimContext *ctx, bool is_replay) {
#if !BSD_CONFIG
  if (ctx == nullptr) {
    return;
  }
  ctx->perf.stq_diag_issue_total++;
  ctx->perf.l1d_req_all++;
  if (is_replay) {
    ctx->perf.stq_diag_issue_replay++;
    ctx->perf.l1d_req_replay++;
  } else {
    ctx->perf.stq_diag_issue_initial++;
    ctx->perf.l1d_req_initial++;
    ctx->perf.dcache_access_num++;
  }
#else
  (void)ctx;
  (void)is_replay;
#endif
}

void count_store_issue_none(SimContext *ctx, uint32_t commit_count,
                            int32_t issued_stq) {
#if !BSD_CONFIG
  if (ctx != nullptr && commit_count > 0 && issued_stq == 0) {
    ctx->perf.stq_diag_issue_none_cycles++;
  }
#else
  (void)ctx;
  (void)commit_count;
  (void)issued_stq;
#endif
}

void count_store_retire(SimContext *ctx, uint32_t committed_count,
                        uint32_t retired_stq) {
#if !BSD_CONFIG
  if (ctx == nullptr || committed_count == 0) {
    return;
  }
  ctx->perf.stq_diag_retire_cycles++;
  ctx->perf.stq_diag_retire_total += retired_stq;
  if (retired_stq == 0) {
    ctx->perf.stq_diag_retire_block_cycles++;
  }
#else
  (void)ctx;
  (void)committed_count;
  (void)retired_stq;
#endif
}

} // namespace lsu_perf
