#pragma once
#include "config.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>

class PerfCount {
public:
  bool perf_start = false;
  bool icache_busy = false;
  uint64_t cycle = 0;
  uint64_t commit_num = 0;
  uint64_t commit_load_num = 0;
  uint64_t commit_store_num = 0;
  bool simtime_snapshot_valid = false;
  uint64_t simtime_snapshot_cycle = 0;
  uint64_t simtime_snapshot_commit_num = 0;
  uint64_t simtime_snapshot_commit_load_num = 0;
  uint64_t simtime_snapshot_commit_store_num = 0;

  struct PeriodicSnapshot {
    uint64_t cycle = 0;
    uint64_t commit_num = 0;
    uint64_t commit_load_num = 0;
    uint64_t commit_store_num = 0;
    uint64_t mmio_inst_count = 0;
    uint64_t stq_same_addr_block_count = 0;
    uint64_t l1d_req_replay = 0;
    uint64_t stall_rob_full_cycles = 0;
    uint64_t dis2ren_not_ready_cycles = 0;
  };
  std::array<PeriodicSnapshot, CONFIG_PERF_PERIODIC_SNAPSHOT_MAX>
      periodic_snapshots = {};
  uint64_t periodic_snapshot_count = 0;
  bool periodic_snapshot_overflow = false;

  uint64_t dcache_access_num = 0;
  uint64_t dcache_miss_num = 0;
  // RealDcache detailed counters
  uint64_t l1d_req_initial = 0; // first requests, excluding replay requests
  uint64_t l1d_req_all = 0; // all dcache requests, including replay requests
  uint64_t l1d_miss_mshr_alloc = 0; // real misses that allocate an MSHR entry
  uint64_t l1d_req_replay = 0; // requests re-issued by replay mechanism
  uint64_t l1d_replay_squash_abort = 0; // replay request failed again
  uint64_t l1d_replay_bank_conflict = 0;
  uint64_t l1d_replay_bank_conflict_load = 0;
  uint64_t l1d_replay_bank_conflict_store = 0;
  uint64_t l1d_replay_mshr_full = 0;
  uint64_t l1d_replay_mshr_full_load = 0;
  uint64_t l1d_replay_mshr_full_store = 0;
  uint64_t l1d_replay_wait_mshr = 0;
  uint64_t l1d_replay_wait_mshr_load = 0;
  uint64_t l1d_replay_wait_mshr_store = 0;
  uint64_t l1d_replay_wait_mshr_hit = 0;
  uint64_t l1d_replay_wait_mshr_first_alloc = 0;
  uint64_t l1d_replay_wait_mshr_fill_wait = 0;
  uint64_t l1d_miss_penalty_total_cycles = 0;
  uint64_t l1d_miss_penalty_samples = 0;
  uint64_t l1d_axi_read_total_cycles = 0;
  uint64_t l1d_axi_read_samples = 0;
  uint64_t l1d_axi_write_total_cycles = 0;
  uint64_t l1d_axi_write_samples = 0;
  uint64_t l1d_mem_inst_total_cycles = 0;
  uint64_t l1d_mem_inst_samples = 0;
  uint64_t mmio_inst_count = 0;
  uint64_t mmio_load_count = 0;
  uint64_t mmio_store_count = 0;
  uint64_t ld_resp_stale_drop_count = 0;
  uint64_t ld_resp_timeout_retry_count = 0;
  uint64_t mmio_head_block_cycles = 0;
  uint64_t ptw_port0_replay_count = 0;
  uint64_t stq_same_addr_block_count = 0;
  uint64_t ld_stlf_check_count = 0;
  uint64_t ld_stlf_block_unknown_store_addr_count = 0;

  uint64_t icache_access_num = 0;
  uint64_t icache_miss_num = 0;
  uint64_t icache_miss_penalty_total_cycles = 0;
  uint64_t icache_miss_penalty_samples = 0;
  uint64_t icache_axi_read_total_cycles = 0;
  uint64_t icache_axi_read_samples = 0;
  uint64_t llc_read_access = 0;
  uint64_t llc_read_hit = 0;
  uint64_t llc_read_miss = 0;
  uint64_t llc_icache_read_access = 0;
  uint64_t llc_icache_read_hit = 0;
  uint64_t llc_icache_read_miss = 0;
  uint64_t llc_dcache_read_access = 0;
  uint64_t llc_dcache_read_hit = 0;
  uint64_t llc_dcache_read_miss = 0;
  uint64_t llc_bypass_read = 0;
  uint64_t llc_write_passthrough = 0;
  uint64_t llc_refill = 0;
  uint64_t llc_mshr_alloc = 0;
  uint64_t llc_mshr_merge = 0;
  uint64_t llc_prefetch_issue = 0;
  uint64_t llc_prefetch_hit = 0;
  uint64_t llc_prefetch_drop_inflight = 0;
  uint64_t llc_prefetch_drop_mshr_full = 0;
  uint64_t llc_prefetch_drop_queue_full = 0;
  uint64_t llc_prefetch_drop_table_hit = 0;
  uint64_t llc_ddr_read_total_cycles = 0;
  uint64_t llc_ddr_read_samples = 0;
  uint64_t llc_ddr_write_total_cycles = 0;
  uint64_t llc_ddr_write_samples = 0;

  uint64_t cond_br_num = 0;
  uint64_t jalr_br_num = 0;
  uint64_t ret_br_num = 0;

  uint64_t cond_mispred_num = 0;
  uint64_t jalr_mispred_num = 0;
  uint64_t ret_mispred_num = 0;

  uint64_t jalr_dir_mispred = 0;
  uint64_t jalr_addr_mispred = 0;

  uint64_t cond_dir_mispred = 0;
  uint64_t cond_addr_mispred = 0;

  uint64_t ret_dir_mispred = 0;
  uint64_t ret_addr_mispred = 0;

  uint64_t idu_tag_stall = 0;
  uint64_t stall_br_id_cycles = 0;
  uint64_t stall_preg_cycles = 0;
  uint64_t stall_rob_full_cycles = 0;
  uint64_t stall_iq_full_cycles = 0;
  uint64_t stall_ldq_full_cycles = 0;
  uint64_t stall_stq_full_cycles = 0;
  uint64_t dis2ren_not_ready_cycles = 0;
  uint64_t dis2ren_not_ready_flush_cycles = 0;
  uint64_t dis2ren_not_ready_rob_cycles = 0;
  uint64_t dis2ren_not_ready_serialize_cycles = 0;
  uint64_t dis2ren_not_ready_dispatch_cycles = 0;
  uint64_t dis2ren_not_ready_older_cycles = 0;
  uint64_t dis2ren_not_ready_dispatch_ldq_cycles = 0;
  uint64_t dis2ren_not_ready_dispatch_stq_cycles = 0;
  uint64_t dis2ren_not_ready_dispatch_iq_cycles = 0;
  uint64_t dis2ren_not_ready_dispatch_iq_detail[IQ_NUM] = {};
  uint64_t dis2ren_not_ready_dispatch_other_cycles = 0;

  uint64_t slots_issued = 0;
  uint64_t slots_backend_bound = 0;
  uint64_t slots_frontend_bound = 0;
  // IB consume-side counters (producer/consumer gap at PreIDU->IDU boundary).
  uint64_t ib_consume_available_slots = 0;
  uint64_t ib_consume_consumed_slots = 0;

  // Level 2 Counters
  uint64_t slots_fetch_latency = 0;
  uint64_t slots_fetch_bandwidth = 0;
  uint64_t slots_mem_bound_lsu = 0;
  uint64_t slots_mem_bound_ldq_full = 0;
  uint64_t slots_mem_bound_stq_full = 0;
  uint64_t slots_core_bound_iq = 0;
  uint64_t slots_core_bound_rob = 0;
  uint64_t slots_frontend_recovery_mispred = 0;
  uint64_t slots_frontend_recovery_flush = 0;
  uint64_t slots_frontend_pure = 0;

  // Level 3 Counters
  uint64_t slots_mem_l1_bound = 0;
  uint64_t slots_mem_ext_bound = 0;
  uint64_t slots_squash_waste = 0;
  uint64_t pending_squash_mispred_slots = 0;
  uint64_t pending_squash_flush_slots = 0;

  // Shared PTW / TLB arbitration counters
  uint64_t ptw_dtlb_req = 0;
  uint64_t ptw_itlb_req = 0;
  uint64_t ptw_dtlb_grant = 0;
  uint64_t ptw_itlb_grant = 0;
  uint64_t ptw_dtlb_resp = 0;
  uint64_t ptw_itlb_resp = 0;
  uint64_t ptw_dtlb_blocked = 0;
  uint64_t ptw_itlb_blocked = 0;
  uint64_t ptw_dtlb_wait_cycle = 0;
  uint64_t ptw_itlb_wait_cycle = 0;

  // Squashed valid instructions on the IDU->Dispatch path
  uint64_t squash_flush_total = 0;
  uint64_t squash_mispred_total = 0;
  uint64_t squash_flush_ren = 0;
  uint64_t squash_mispred_ren = 0;
  uint64_t squash_flush_dis = 0;
  uint64_t squash_mispred_dis = 0;
  // Key front-end counters
  uint64_t front2back_fetched_inst_total = 0;
  uint64_t front2back_read_cycle_total = 0;
  uint64_t front2back_read_enable_cycle_total = 0;
  uint64_t front2back_read_empty_cycle_total = 0;
  uint64_t front_fetch_addr_block_ready0_empty1_cycle_total = 0;
  uint64_t front_icache_req_cycle_total = 0;
  uint64_t front_icache_complete_cycle_total = 0;
  uint64_t front_bpu_issue_cycle_total = 0;
  uint64_t front_bpu_can_run_cycle_total = 0;
  uint64_t front_bpu_no_issue_when_can_run_cycle_total = 0;
  // Frontend handshake imbalance (to locate who is the bottleneck)
  // - icache_wait_bpu: icache side ready but no fetch address to consume
  // - bpu_wait_icache: bpu side blocked because fetch-address queue is full
  uint64_t front_icache_wait_bpu_cycle_total = 0;
  uint64_t front_bpu_wait_icache_cycle_total = 0;
  uint64_t front_predecode_gate_block_fifo_empty_cycle_total = 0;
  uint64_t front_predecode_gate_block_ptab_empty_cycle_total = 0;
  uint64_t front_predecode_gate_block_reset_refetch_cycle_total = 0;

  // Instruction buffer write-side metrics (frontend supply capability)
  uint64_t ib_write_inst_total = 0;
  uint64_t ib_write_cycle_total = 0;
  uint64_t ib_blocked_cycles = 0;
  uint64_t ftq_blocked_cycles = 0;

  void perf_reset() {
    cycle = 0;
    commit_num = 0;
    commit_load_num = 0;
    commit_store_num = 0;
    simtime_snapshot_valid = false;
    simtime_snapshot_cycle = 0;
    simtime_snapshot_commit_num = 0;
    simtime_snapshot_commit_load_num = 0;
    simtime_snapshot_commit_store_num = 0;
    periodic_snapshot_count = 0;
    periodic_snapshot_overflow = false;
    periodic_snapshots = {};
    // dcache
    dcache_access_num = 0;
    dcache_miss_num = 0;
    l1d_req_initial = 0;
    l1d_req_all = 0;
    l1d_miss_mshr_alloc = 0;
    l1d_req_replay = 0;
    l1d_replay_squash_abort = 0;
    l1d_replay_bank_conflict = 0;
    l1d_replay_bank_conflict_load = 0;
    l1d_replay_bank_conflict_store = 0;
    l1d_replay_mshr_full = 0;
    l1d_replay_mshr_full_load = 0;
    l1d_replay_mshr_full_store = 0;
    l1d_replay_wait_mshr = 0;
    l1d_replay_wait_mshr_load = 0;
    l1d_replay_wait_mshr_store = 0;
    l1d_replay_wait_mshr_hit = 0;
    l1d_replay_wait_mshr_first_alloc = 0;
    l1d_replay_wait_mshr_fill_wait = 0;
    l1d_miss_penalty_total_cycles = 0;
    l1d_miss_penalty_samples = 0;
    l1d_axi_read_total_cycles = 0;
    l1d_axi_read_samples = 0;
    l1d_axi_write_total_cycles = 0;
    l1d_axi_write_samples = 0;
    l1d_mem_inst_total_cycles = 0;
    l1d_mem_inst_samples = 0;
    mmio_inst_count = 0;
    mmio_load_count = 0;
    mmio_store_count = 0;
    ld_resp_stale_drop_count = 0;
    ld_resp_timeout_retry_count = 0;
    mmio_head_block_cycles = 0;
    ptw_port0_replay_count = 0;
    stq_same_addr_block_count = 0;
    ld_stlf_check_count = 0;
    ld_stlf_block_unknown_store_addr_count = 0;
    icache_access_num = 0;
    icache_miss_num = 0;
    icache_miss_penalty_total_cycles = 0;
    icache_miss_penalty_samples = 0;
    icache_axi_read_total_cycles = 0;
    icache_axi_read_samples = 0;
    llc_read_access = 0;
    llc_read_hit = 0;
    llc_read_miss = 0;
    llc_icache_read_access = 0;
    llc_icache_read_hit = 0;
    llc_icache_read_miss = 0;
    llc_dcache_read_access = 0;
    llc_dcache_read_hit = 0;
    llc_dcache_read_miss = 0;
    llc_bypass_read = 0;
    llc_write_passthrough = 0;
    llc_refill = 0;
    llc_mshr_alloc = 0;
    llc_mshr_merge = 0;
    llc_prefetch_issue = 0;
    llc_prefetch_hit = 0;
    llc_prefetch_drop_inflight = 0;
    llc_prefetch_drop_mshr_full = 0;
    llc_prefetch_drop_queue_full = 0;
    llc_prefetch_drop_table_hit = 0;
    llc_ddr_read_total_cycles = 0;
    llc_ddr_read_samples = 0;
    llc_ddr_write_total_cycles = 0;
    llc_ddr_write_samples = 0;

    // bpu
    cond_br_num = 0;
    jalr_br_num = 0;
    ret_br_num = 0;

    cond_mispred_num = 0;
    jalr_mispred_num = 0;
    ret_mispred_num = 0;

    jalr_dir_mispred = 0;
    jalr_addr_mispred = 0;

    cond_dir_mispred = 0;
    cond_addr_mispred = 0;

    ret_dir_mispred = 0;
    ret_addr_mispred = 0;
    idu_tag_stall = 0;
    stall_br_id_cycles = 0;
    stall_preg_cycles = 0;
    stall_rob_full_cycles = 0;
    stall_iq_full_cycles = 0;
    stall_ldq_full_cycles = 0;
    stall_stq_full_cycles = 0;
    dis2ren_not_ready_cycles = 0;
    dis2ren_not_ready_flush_cycles = 0;
    dis2ren_not_ready_rob_cycles = 0;
    dis2ren_not_ready_serialize_cycles = 0;
    dis2ren_not_ready_dispatch_cycles = 0;
    dis2ren_not_ready_older_cycles = 0;
    dis2ren_not_ready_dispatch_ldq_cycles = 0;
    dis2ren_not_ready_dispatch_stq_cycles = 0;
    dis2ren_not_ready_dispatch_iq_cycles = 0;
    dis2ren_not_ready_dispatch_other_cycles = 0;
    for (auto &v : dis2ren_not_ready_dispatch_iq_detail)
      v = 0;

    slots_issued = 0;
    slots_backend_bound = 0;
    slots_frontend_bound = 0;
    ib_consume_available_slots = 0;
    ib_consume_consumed_slots = 0;

    slots_fetch_latency = 0;
    slots_fetch_bandwidth = 0;
    slots_mem_bound_lsu = 0;
    slots_mem_bound_ldq_full = 0;
    slots_mem_bound_stq_full = 0;
    slots_core_bound_iq = 0;
    slots_core_bound_rob = 0;
    slots_frontend_recovery_mispred = 0;
    slots_frontend_recovery_flush = 0;
    slots_frontend_pure = 0;

    slots_mem_l1_bound = 0;
    slots_mem_ext_bound = 0;
    slots_squash_waste = 0;
    pending_squash_mispred_slots = 0;
    pending_squash_flush_slots = 0;

    ptw_dtlb_req = 0;
    ptw_itlb_req = 0;
    ptw_dtlb_grant = 0;
    ptw_itlb_grant = 0;
    ptw_dtlb_resp = 0;
    ptw_itlb_resp = 0;
    ptw_dtlb_blocked = 0;
    ptw_itlb_blocked = 0;
    ptw_dtlb_wait_cycle = 0;
    ptw_itlb_wait_cycle = 0;

    squash_flush_total = 0;
    squash_mispred_total = 0;
    squash_flush_ren = 0;
    squash_mispred_ren = 0;
    squash_flush_dis = 0;
    squash_mispred_dis = 0;
    front2back_fetched_inst_total = 0;
    front2back_read_cycle_total = 0;
    front2back_read_enable_cycle_total = 0;
    front2back_read_empty_cycle_total = 0;
    front_fetch_addr_block_ready0_empty1_cycle_total = 0;
    front_icache_req_cycle_total = 0;
    front_icache_complete_cycle_total = 0;
    front_bpu_issue_cycle_total = 0;
    front_bpu_can_run_cycle_total = 0;
    front_bpu_no_issue_when_can_run_cycle_total = 0;
    front_icache_wait_bpu_cycle_total = 0;
    front_bpu_wait_icache_cycle_total = 0;
    front_predecode_gate_block_fifo_empty_cycle_total = 0;
    front_predecode_gate_block_ptab_empty_cycle_total = 0;
    front_predecode_gate_block_reset_refetch_cycle_total = 0;
    ib_write_inst_total = 0;
    ib_write_cycle_total = 0;
    ib_blocked_cycles = 0;
    ftq_blocked_cycles = 0;
  }

  void perf_print() {
    if (CONFIG_PERF_SNAPSHOT_SIM_TIME > 0) {
      if (simtime_snapshot_valid) {
        printf("\033[38;5;34msim-time target(cycle)= %llu, committed(total/load/store)= %llu / %llu / %llu\033[0m\n",
               static_cast<unsigned long long>(simtime_snapshot_cycle),
               static_cast<unsigned long long>(simtime_snapshot_commit_num),
               static_cast<unsigned long long>(simtime_snapshot_commit_load_num),
               static_cast<unsigned long long>(simtime_snapshot_commit_store_num));
      } else {
        printf("\033[38;5;34msim-time target(cycle)= %llu, committed(total/load/store)= not reached\033[0m\n",
               static_cast<unsigned long long>(CONFIG_PERF_SNAPSHOT_SIM_TIME));
      }
    }
    printf("\033[38;5;34msim-time(cycle)= %ld, committed(total/load/store)= %ld / %ld / %ld\033[0m\n",
           cycle, commit_num, commit_load_num, commit_store_num);
    printf("\033[38;5;34minstruction num: %ld\033[0m\n", commit_num);
    printf("\033[38;5;34mcycle       num: %ld\033[0m\n", cycle);
    printf("\033[38;5;34mipc            : %f\033[0m\n",
           (double)commit_num / cycle);
    printf("\n");
    perf_print_periodic_snapshots();
    perf_print_dcache();
    perf_print_icache();
    perf_print_llc();
    perf_print_ptw();
    perf_print_branch();
    perf_print_squash();
    perf_print_frontend_fetch();
    perf_print_resource_stall();
    perf_print_tma();
  }

  void perf_maybe_capture_simtime_snapshot() {
    perf_maybe_capture_periodic_snapshot();
    constexpr uint64_t kTargetCycle =
        static_cast<uint64_t>(CONFIG_PERF_SNAPSHOT_SIM_TIME);
    if (kTargetCycle == 0 || simtime_snapshot_valid) {
      return;
    }
    if (cycle < kTargetCycle) {
      return;
    }
    simtime_snapshot_valid = true;
    simtime_snapshot_cycle = cycle;
    simtime_snapshot_commit_num = commit_num;
    simtime_snapshot_commit_load_num = commit_load_num;
    simtime_snapshot_commit_store_num = commit_store_num;
  }

  void perf_maybe_capture_periodic_snapshot() {
#if CONFIG_PERF_PERIODIC_SNAPSHOT_INTERVAL == 0 ||                             \
    CONFIG_PERF_PERIODIC_SNAPSHOT_MAX == 0
    return;
#else
    constexpr uint64_t kInterval =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_INTERVAL);
    constexpr uint64_t kBegin =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_BEGIN);
    constexpr uint64_t kEnd =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_END);
    constexpr uint64_t kMax =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_MAX);
    if (cycle < kBegin || cycle > kEnd) {
      return;
    }
    if (((cycle - kBegin) % kInterval) != 0) {
      return;
    }
    if (periodic_snapshot_count >= kMax) {
      periodic_snapshot_overflow = true;
      return;
    }

    auto &s = periodic_snapshots[periodic_snapshot_count++];
    s.cycle = cycle;
    s.commit_num = commit_num;
    s.commit_load_num = commit_load_num;
    s.commit_store_num = commit_store_num;
    s.mmio_inst_count = mmio_inst_count;
    s.stq_same_addr_block_count = stq_same_addr_block_count;
    s.l1d_req_replay = l1d_req_replay;
    s.stall_rob_full_cycles = stall_rob_full_cycles;
    s.dis2ren_not_ready_cycles = dis2ren_not_ready_cycles;
#endif
  }

  void perf_print_periodic_snapshots() {
    constexpr uint64_t kInterval =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_INTERVAL);
    constexpr uint64_t kBegin =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_BEGIN);
    constexpr uint64_t kEnd =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_END);
    constexpr uint64_t kMax =
        static_cast<uint64_t>(CONFIG_PERF_PERIODIC_SNAPSHOT_MAX);
    if (kInterval == 0) {
      return;
    }

    printf("\033[38;5;34m*********PERIODIC SNAPSHOT*********\033[0m\n");
    if (periodic_snapshot_count == 0) {
      printf("\033[38;5;34mperiodic snapshot: no sample captured (begin=%llu end=%llu interval=%llu)\033[0m\n",
             static_cast<unsigned long long>(kBegin),
             static_cast<unsigned long long>(kEnd),
             static_cast<unsigned long long>(kInterval));
      printf("\n");
      return;
    }

    printf("\033[38;5;34mperiodic snapshot cfg: begin=%llu end=%llu interval=%llu max=%llu samples=%llu%s\033[0m\n",
           static_cast<unsigned long long>(kBegin),
           static_cast<unsigned long long>(kEnd),
           static_cast<unsigned long long>(kInterval),
           static_cast<unsigned long long>(kMax),
           static_cast<unsigned long long>(periodic_snapshot_count),
           periodic_snapshot_overflow ? " (truncated)" : "");

    uint64_t prev_cycle = 0;
    uint64_t prev_commit = 0;
    uint64_t prev_load = 0;
    uint64_t prev_store = 0;
    uint64_t prev_mmio = 0;
    uint64_t prev_stq_blk = 0;
    uint64_t prev_replay = 0;
    bool first = true;

    for (uint64_t i = 0; i < periodic_snapshot_count; i++) {
      const auto &s = periodic_snapshots[i];
      const uint64_t delta_cycle = first ? s.cycle : (s.cycle - prev_cycle);
      const uint64_t delta_commit =
          first ? s.commit_num : (s.commit_num - prev_commit);
      const uint64_t delta_load =
          first ? s.commit_load_num : (s.commit_load_num - prev_load);
      const uint64_t delta_store =
          first ? s.commit_store_num : (s.commit_store_num - prev_store);
      const uint64_t delta_mmio =
          first ? s.mmio_inst_count : (s.mmio_inst_count - prev_mmio);
      const uint64_t delta_stq_blk = first
                                         ? s.stq_same_addr_block_count
                                         : (s.stq_same_addr_block_count -
                                            prev_stq_blk);
      const uint64_t delta_replay =
          first ? s.l1d_req_replay : (s.l1d_req_replay - prev_replay);
      const double window_ipc =
          (delta_cycle == 0)
              ? 0.0
              : static_cast<double>(delta_commit) /
                    static_cast<double>(delta_cycle);

      printf("\033[38;5;34m  [snap %4llu] cyc=%llu commit=%llu (+%llu) ld=%llu (+%llu) st=%llu (+%llu) ipc_win=%.3f mmio=%llu (+%llu) stq_blk=%llu (+%llu) replay=%llu (+%llu)\033[0m\n",
             static_cast<unsigned long long>(i),
             static_cast<unsigned long long>(s.cycle),
             static_cast<unsigned long long>(s.commit_num),
             static_cast<unsigned long long>(delta_commit),
             static_cast<unsigned long long>(s.commit_load_num),
             static_cast<unsigned long long>(delta_load),
             static_cast<unsigned long long>(s.commit_store_num),
             static_cast<unsigned long long>(delta_store), window_ipc,
             static_cast<unsigned long long>(s.mmio_inst_count),
             static_cast<unsigned long long>(delta_mmio),
             static_cast<unsigned long long>(s.stq_same_addr_block_count),
             static_cast<unsigned long long>(delta_stq_blk),
             static_cast<unsigned long long>(s.l1d_req_replay),
             static_cast<unsigned long long>(delta_replay));

      prev_cycle = s.cycle;
      prev_commit = s.commit_num;
      prev_load = s.commit_load_num;
      prev_store = s.commit_store_num;
      prev_mmio = s.mmio_inst_count;
      prev_stq_blk = s.stq_same_addr_block_count;
      prev_replay = s.l1d_req_replay;
      first = false;
    }
    printf("\n");
  }

  void perf_print_dcache() {
    printf("\033[38;5;34m*********L1D COUNTER***************\033[0m\n");
    const double l1d_hit_rate =
        (l1d_req_initial == 0)
            ? 1.0
            : 1.0 - static_cast<double>(l1d_miss_mshr_alloc) /
                        static_cast<double>(l1d_req_initial);
    const double l1d_miss_rate = 1.0 - l1d_hit_rate;
    const double l1d_mpki =
        (commit_num == 0)
            ? 0.0
            : static_cast<double>(l1d_miss_mshr_alloc) * 1000.0 /
                  static_cast<double>(commit_num);
    const double avg_miss_penalty =
        (l1d_miss_penalty_samples == 0)
            ? 0.0
            : static_cast<double>(l1d_miss_penalty_total_cycles) /
                  static_cast<double>(l1d_miss_penalty_samples);
    const double amat = 1.0 + l1d_miss_rate * avg_miss_penalty;
    const double avg_axi_read =
        (l1d_axi_read_samples == 0)
            ? 0.0
            : static_cast<double>(l1d_axi_read_total_cycles) /
                  static_cast<double>(l1d_axi_read_samples);
    const double avg_axi_write =
        (l1d_axi_write_samples == 0)
            ? 0.0
            : static_cast<double>(l1d_axi_write_total_cycles) /
                  static_cast<double>(l1d_axi_write_samples);
    const double avg_mem_inst_latency =
        (l1d_mem_inst_samples == 0)
            ? 0.0
            : static_cast<double>(l1d_mem_inst_total_cycles) /
                  static_cast<double>(l1d_mem_inst_samples);
    const uint64_t l1d_replay_reason_total =
        l1d_replay_bank_conflict + l1d_replay_mshr_full + l1d_replay_wait_mshr;
    const double l1d_replay_bank_conflict_ratio =
        (l1d_replay_reason_total == 0)
            ? 0.0
            : static_cast<double>(l1d_replay_bank_conflict) * 100.0 /
                  static_cast<double>(l1d_replay_reason_total);
    const double l1d_replay_mshr_full_ratio =
        (l1d_replay_reason_total == 0)
            ? 0.0
            : static_cast<double>(l1d_replay_mshr_full) * 100.0 /
                  static_cast<double>(l1d_replay_reason_total);
    const double l1d_replay_wait_mshr_ratio =
        (l1d_replay_reason_total == 0)
            ? 0.0
            : static_cast<double>(l1d_replay_wait_mshr) * 100.0 /
                  static_cast<double>(l1d_replay_reason_total);
    printf("\033[38;5;34mL1D_REQ_INITIAL      : %ld\033[0m\n", l1d_req_initial);
    printf("\033[38;5;34mL1D_REQ_ALL          : %ld\033[0m\n", l1d_req_all);
    printf("\033[38;5;34mL1D_MISS_MSHR_ALLOC  : %ld\033[0m\n", l1d_miss_mshr_alloc);
    printf("\033[38;5;34mL1D_REQ_REPLAY       : %ld\033[0m\n", l1d_req_replay);
    printf("\033[38;5;34mL1D_REPLAY_SQUASH    : %ld\033[0m\n",
           l1d_replay_squash_abort);
    printf("\033[38;5;34mL1D_REPLAY_BANK_CONFLICT : %ld\033[0m\n",
           l1d_replay_bank_conflict);
    printf("\033[38;5;34m  - LOAD                 : %ld\033[0m\n",
           l1d_replay_bank_conflict_load);
    printf("\033[38;5;34m  - STORE                : %ld\033[0m\n",
           l1d_replay_bank_conflict_store);
    printf("\033[38;5;34mL1D_REPLAY_MSHR_FULL     : %ld\033[0m\n",
           l1d_replay_mshr_full);
    printf("\033[38;5;34m  - LOAD                 : %ld\033[0m\n",
           l1d_replay_mshr_full_load);
    printf("\033[38;5;34m  - STORE                : %ld\033[0m\n",
           l1d_replay_mshr_full_store);
    printf("\033[38;5;34mL1D_REPLAY_WAIT_MSHR     : %ld\033[0m\n",
           l1d_replay_wait_mshr);
    printf("\033[38;5;34m  - LOAD                 : %ld\033[0m\n",
           l1d_replay_wait_mshr_load);
    printf("\033[38;5;34m  - STORE                : %ld\033[0m\n",
           l1d_replay_wait_mshr_store);
    printf("\033[38;5;34m  - WAIT_MSHR_HITLINE    : %ld\033[0m\n",
           l1d_replay_wait_mshr_hit);
    printf("\033[38;5;34m  - WAIT_MSHR_FIRST_ALLOC: %ld\033[0m\n",
           l1d_replay_wait_mshr_first_alloc);
    printf("\033[38;5;34m  - WAIT_MSHR_FILL_WAIT  : %ld\033[0m\n",
           l1d_replay_wait_mshr_fill_wait);
    printf("\033[38;5;34mL1D_REPLAY_REASON_TOTAL  : %ld\033[0m\n",
           l1d_replay_reason_total);
    printf("\033[38;5;34m  - BANK_CONFLICT_RATIO  : %.2f%%\033[0m\n",
           l1d_replay_bank_conflict_ratio);
    printf("\033[38;5;34m  - MSHR_FULL_RATIO      : %.2f%%\033[0m\n",
           l1d_replay_mshr_full_ratio);
    printf("\033[38;5;34m  - WAIT_MSHR_RATIO      : %.2f%%\033[0m\n",
           l1d_replay_wait_mshr_ratio);
    printf("\033[38;5;34mL1D Hit Rate         : %.6f\033[0m\n", l1d_hit_rate);
    printf("\033[38;5;34mL1D MPKI             : %.6f\033[0m\n", l1d_mpki);
    printf("\033[38;5;34mL1D AMAT(cycles)     : %.6f (hit=1cy assumption)\033[0m\n",
           amat);
    printf("\033[38;5;34mAvg Miss Penalty     : %.6f cycles (samples=%ld)\033[0m\n",
           avg_miss_penalty, l1d_miss_penalty_samples);
    printf("\033[38;5;34mAvg AXI Read Latency : %.6f cycles (samples=%ld)\033[0m\n",
           avg_axi_read, l1d_axi_read_samples);
    printf("\033[38;5;34mAvg AXI Write Latency: %.6f cycles (samples=%ld)\033[0m\n",
           avg_axi_write, l1d_axi_write_samples);
    printf("\033[38;5;34mAvg Mem-Inst Latency : %.6f cycles (samples=%ld)\033[0m\n",
           avg_mem_inst_latency, l1d_mem_inst_samples);
    printf("\033[38;5;34mMMIO Inst Count      : %ld\033[0m\n", mmio_inst_count);
    printf("\033[38;5;34mMMIO Load Count      : %ld\033[0m\n", mmio_load_count);
    printf("\033[38;5;34mMMIO Store Count     : %ld\033[0m\n", mmio_store_count);
    printf("\033[38;5;34mLD Resp Stale Drop   : %ld\033[0m\n",
           ld_resp_stale_drop_count);
    printf("\033[38;5;34mLD Resp TimeoutRetry : %ld\033[0m\n",
           ld_resp_timeout_retry_count);
    printf("\033[38;5;34mMMIO Head Block Cyc  : %ld\033[0m\n",
           mmio_head_block_cycles);
    printf("\033[38;5;34mPTW Port0 Replay Cnt : %ld\033[0m\n",
           ptw_port0_replay_count);
    printf("\033[38;5;34mSTQ SameAddr Block   : %ld\033[0m\n",
           stq_same_addr_block_count);
    const double ld_stlf_unknown_block_ratio =
        (ld_stlf_check_count == 0)
            ? 0.0
            : static_cast<double>(ld_stlf_block_unknown_store_addr_count) *
                  100.0 / static_cast<double>(ld_stlf_check_count);
    printf("\033[38;5;34mLD STLF Check Cnt    : %ld\033[0m\n",
           ld_stlf_check_count);
    printf("\033[38;5;34mLD Block Unknown STA : %ld (%.4f%% of checks)\033[0m\n",
           ld_stlf_block_unknown_store_addr_count, ld_stlf_unknown_block_ratio);
    printf("\n");
  }

  void perf_print_icache() {
    printf("\033[38;5;34m*********L1I COUNTER***************\033[0m\n");
    const double icache_hit_rate =
        (icache_access_num == 0)
            ? 1.0
            : 1.0 - static_cast<double>(icache_miss_num) /
                        static_cast<double>(icache_access_num);
    const double icache_miss_rate = 1.0 - icache_hit_rate;
    const double avg_miss_penalty =
        (icache_miss_penalty_samples == 0)
            ? 0.0
            : static_cast<double>(icache_miss_penalty_total_cycles) /
                  static_cast<double>(icache_miss_penalty_samples);
    const double avg_axi_read =
        (icache_axi_read_samples == 0)
            ? 0.0
            : static_cast<double>(icache_axi_read_total_cycles) /
                  static_cast<double>(icache_axi_read_samples);
    const double amat = 1.0 + icache_miss_rate * avg_miss_penalty;
    printf("\033[38;5;34mL1I accuracy    : %f\033[0m\n", icache_hit_rate);
    printf("\033[38;5;34mL1I access      : %ld\033[0m\n", icache_access_num);
    printf("\033[38;5;34mL1I hit         : %ld\033[0m\n",
           icache_access_num - icache_miss_num);
    printf("\033[38;5;34mL1I miss        : %ld\033[0m\n", icache_miss_num);
    printf("\033[38;5;34mL1I AMAT(cycles): %.6f (hit=1cy assumption)\033[0m\n",
           amat);
    printf("\033[38;5;34mAvg Miss Penalty: %.6f cycles (samples=%ld)\033[0m\n",
           avg_miss_penalty, icache_miss_penalty_samples);
    printf("\033[38;5;34mAvg AXI Read    : %.6f cycles (samples=%ld)\033[0m\n",
           avg_axi_read, icache_axi_read_samples);
    printf("\n");
  }

  void perf_print_llc() {
    printf("\033[38;5;34m*********LLC COUNTER**************\033[0m\n");
#if CONFIG_AXI_LLC_ENABLE
    const double llc_acc =
        llc_read_access ? static_cast<double>(llc_read_hit) / llc_read_access
                        : 1.0;
    const double llc_ddr_read_avg =
        llc_ddr_read_samples
            ? static_cast<double>(llc_ddr_read_total_cycles) /
                  static_cast<double>(llc_ddr_read_samples)
            : 0.0;
    const double llc_ddr_write_avg =
        llc_ddr_write_samples
            ? static_cast<double>(llc_ddr_write_total_cycles) /
                  static_cast<double>(llc_ddr_write_samples)
            : 0.0;
    printf("\033[38;5;34mllc read acc    : %f\033[0m\n", llc_acc);
    printf("\033[38;5;34mllc read access : %ld\033[0m\n", llc_read_access);
    printf("\033[38;5;34mllc read hit    : %ld\033[0m\n", llc_read_hit);
    printf("\033[38;5;34mllc read miss   : %ld\033[0m\n", llc_read_miss);
    printf("\033[38;5;34mllc l1i a/h/m   : %ld / %ld / %ld\033[0m\n",
           llc_icache_read_access, llc_icache_read_hit, llc_icache_read_miss);
    printf("\033[38;5;34mllc l1d a/h/m   : %ld / %ld / %ld\033[0m\n",
           llc_dcache_read_access, llc_dcache_read_hit, llc_dcache_read_miss);
    printf("\033[38;5;34mllc bypass read : %ld\033[0m\n", llc_bypass_read);
    printf("\033[38;5;34mllc refill      : %ld\033[0m\n", llc_refill);
    printf("\033[38;5;34mllc mshr alloc  : %ld\033[0m\n", llc_mshr_alloc);
    printf("\033[38;5;34mllc mshr merge  : %ld\033[0m\n", llc_mshr_merge);
    printf("\033[38;5;34mllc wr passthru : %ld\033[0m\n",
           llc_write_passthrough);
    printf("\033[38;5;34mpf issue/hit    : %ld / %ld\033[0m\n",
           llc_prefetch_issue, llc_prefetch_hit);
    printf("\033[38;5;34mpf drop inflight/full : %ld / %ld\033[0m\n",
           llc_prefetch_drop_inflight, llc_prefetch_drop_mshr_full);
    printf("\033[38;5;34mpf drop queue/hit     : %ld / %ld\033[0m\n",
           llc_prefetch_drop_queue_full, llc_prefetch_drop_table_hit);
    printf("\033[38;5;34mllc->ddr read avg : %.6f cycles (samples=%ld)\033[0m\n",
           llc_ddr_read_avg, llc_ddr_read_samples);
    printf("\033[38;5;34mllc->ddr write avg: %.6f cycles (samples=%ld)\033[0m\n",
           llc_ddr_write_avg, llc_ddr_write_samples);
#else
    printf("\033[38;5;34mllc status      : disabled\033[0m\n");
#endif
    printf("\n");
  }

  void perf_print_branch() {
    printf("\033[38;5;34m*********BPU COUNTER************\033[0m\n");
    printf("\033[38;5;34mbpu   accuracy : %f\033[0m\n\n",
           1 - (cond_mispred_num + jalr_mispred_num + ret_mispred_num) /
                   (double)(cond_br_num + jalr_br_num + ret_br_num));

    printf("\033[38;5;34mjalr  accuracy : %f\033[0m\n",
           1 - (jalr_mispred_num) / (double)(jalr_br_num));
    printf("\033[38;5;34mnum        : %ld\033[0m\n", jalr_br_num);
    printf("\033[38;5;34mmispred    : %ld\033[0m\n", jalr_mispred_num);
    printf("\033[38;5;34maddr error : %ld\033[0m\n", jalr_addr_mispred);
    printf("\033[38;5;34mdir  error : %ld\033[0m\n", jalr_dir_mispred);
    printf("\n");

    printf("\033[38;5;34mbr    accuracy : %f\033[0m\n",
           1 - (cond_mispred_num) / (double)(cond_br_num));
    printf("\033[38;5;34mnum        : %ld\033[0m\n", cond_br_num);
    printf("\033[38;5;34mmispred    : %ld\033[0m\n", cond_mispred_num);
    printf("\033[38;5;34maddr error : %ld\033[0m\n", cond_addr_mispred);
    printf("\033[38;5;34mdir  error : %ld\033[0m\n", cond_dir_mispred);
    printf("\n");

    printf("\033[38;5;34mret    accuracy : %f\033[0m\n",
           1 - (ret_mispred_num) / (double)(ret_br_num));
    printf("\033[38;5;34mnum        : %ld\033[0m\n", ret_br_num);
    printf("\033[38;5;34mmispred    : %ld\033[0m\n", ret_mispred_num);
    printf("\033[38;5;34maddr error : %ld\033[0m\n", ret_addr_mispred);
    printf("\033[38;5;34mdir  error : %ld\033[0m\n", ret_dir_mispred);
    printf("\n");
  }
  void perf_print_ptw() {
    printf("\033[38;5;34m*********PTW/ARB COUNTER***********\033[0m\n");
    printf("\033[38;5;34mDTLB req/grant/resp : %ld / %ld / %ld\033[0m\n",
           ptw_dtlb_req, ptw_dtlb_grant, ptw_dtlb_resp);
    printf("\033[38;5;34mITLB req/grant/resp : %ld / %ld / %ld\033[0m\n",
           ptw_itlb_req, ptw_itlb_grant, ptw_itlb_resp);
    printf("\033[38;5;34mDTLB blocked        : %ld\033[0m\n", ptw_dtlb_blocked);
    printf("\033[38;5;34mITLB blocked        : %ld\033[0m\n", ptw_itlb_blocked);
    printf("\033[38;5;34mDTLB wait cycles    : %ld\033[0m\n", ptw_dtlb_wait_cycle);
    printf("\033[38;5;34mITLB wait cycles    : %ld\033[0m\n", ptw_itlb_wait_cycle);
    printf("\n");
  }

  void perf_print_squash() {
    printf("\033[38;5;34m*********SQUASH COUNTER************\033[0m\n");
    printf("\033[38;5;34msquash flush total      : %ld\033[0m\n",
           squash_flush_total);
    printf("\033[38;5;34m  - from ren            : %ld\033[0m\n",
           squash_flush_ren);
    printf("\033[38;5;34m  - from dispatch       : %ld\033[0m\n",
           squash_flush_dis);
    printf("\033[38;5;34msquash mispred total    : %ld\033[0m\n",
           squash_mispred_total);
    printf("\033[38;5;34m  - from ren            : %ld\033[0m\n",
           squash_mispred_ren);
    printf("\033[38;5;34m  - from dispatch       : %ld\033[0m\n",
           squash_mispred_dis);
    printf("\033[38;5;34mpending squash slots    : flush=%ld mispred=%ld\033[0m\n",
           pending_squash_flush_slots, pending_squash_mispred_slots);
    printf("\n");
  }

  void perf_print_frontend_fetch() {
    printf("\033[38;5;34m*********FRONTEND FETCH***********\033[0m\n");
    const double avg_ib_write_per_cycle =
        cycle ? static_cast<double>(ib_write_inst_total) / cycle : 0.0;
    const double avg_ib_write_per_write_cycle =
        ib_write_cycle_total
            ? static_cast<double>(ib_write_inst_total) / ib_write_cycle_total
            : 0.0;
    printf("\033[38;5;34mavg inst / sim cycle         : %.4f\033[0m\n",
           avg_ib_write_per_cycle);
    printf("\033[38;5;34mavg inst / ib write cycle    : %.4f\033[0m\n",
           avg_ib_write_per_write_cycle);
    printf("\033[38;5;34mib blocked cycles            : %ld\033[0m\n",
           ib_blocked_cycles);
    printf("\033[38;5;34mftq blocked cycles           : %ld\033[0m\n",
           ftq_blocked_cycles);
    const double icache_wait_bpu_pct =
        cycle ? static_cast<double>(front_icache_wait_bpu_cycle_total) * 100.0 /
                    cycle
              : 0.0;
    const double bpu_wait_icache_pct =
        cycle ? static_cast<double>(front_bpu_wait_icache_cycle_total) * 100.0 /
                    cycle
              : 0.0;
    printf("\033[38;5;34micache wait bpu cycles       : %ld (%.2f%%)\033[0m\n",
           front_icache_wait_bpu_cycle_total, icache_wait_bpu_pct);
    printf("\033[38;5;34mbpu wait icache cycles       : %ld (%.2f%%)\033[0m\n",
           front_bpu_wait_icache_cycle_total, bpu_wait_icache_pct);
    printf("\n");
  }

  void perf_print_resource_stall() {
    printf("\033[38;5;34m*********RESOURCE STALL************\033[0m\n");
    printf("\033[38;5;34mbr id stall cycles      : %ld\033[0m\n",
           stall_br_id_cycles);
    printf("\033[38;5;34mpreg stall cycles       : %ld\033[0m\n",
           stall_preg_cycles);
    printf("\033[38;5;34mrob full stall cycles   : %ld\033[0m\n",
           stall_rob_full_cycles);
    printf("\033[38;5;34miq full stall cycles    : %ld\033[0m\n",
           stall_iq_full_cycles);
    printf("\033[38;5;34mldq full stall cycles   : %ld\033[0m\n",
           stall_ldq_full_cycles);
    printf("\033[38;5;34mstq full stall cycles   : %ld\033[0m\n",
           stall_stq_full_cycles);
    printf("\033[38;5;34mdis2ren not ready cycles: %ld\033[0m\n",
           dis2ren_not_ready_cycles);
    printf("\033[38;5;34m  - dis2ren flush/stall : %ld\033[0m\n",
           dis2ren_not_ready_flush_cycles);
    printf("\033[38;5;34m  - dis2ren rob         : %ld\033[0m\n",
           dis2ren_not_ready_rob_cycles);
    printf("\033[38;5;34m  - dis2ren serialize   : %ld\033[0m\n",
           dis2ren_not_ready_serialize_cycles);
    printf("\033[38;5;34m  - dis2ren dispatch    : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_cycles);
    printf("\033[38;5;34m  - dis2ren older       : %ld\033[0m\n",
           dis2ren_not_ready_older_cycles);
    printf("\033[38;5;34m    - dispatch ldq      : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_ldq_cycles);
    printf("\033[38;5;34m    - dispatch stq      : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_stq_cycles);
    printf("\033[38;5;34m    - dispatch iq total : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_cycles);
    printf("\033[38;5;34m      - iq int          : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_detail[IQ_INT]);
    printf("\033[38;5;34m      - iq ld           : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_detail[IQ_LD]);
    printf("\033[38;5;34m      - iq sta          : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_detail[IQ_STA]);
    printf("\033[38;5;34m      - iq std          : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_detail[IQ_STD]);
    printf("\033[38;5;34m      - iq br           : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_iq_detail[IQ_BR]);
    printf("\033[38;5;34m    - dispatch other    : %ld\033[0m\n",
           dis2ren_not_ready_dispatch_other_cycles);
    printf("\n");
  }

  void perf_print_tma() {
    printf(
        "\033[38;5;34m*********Top-Down Analysis (Level 1)************\033[0m\n");

    // Total slots available
    uint64_t total_slots = slots_issued + slots_backend_bound + slots_frontend_bound;
    if (total_slots == 0)
      total_slots = 1; // Avoid divide by zero

    // Frontend Bound
    double frontend_bound_pct = (double)slots_frontend_bound / total_slots;

    // Backend Bound
    double backend_bound_pct = (double)slots_backend_bound / total_slots;

    // Bad Speculation
    int64_t bad_speculation_slots = (int64_t)slots_issued - (int64_t)commit_num;
    if (bad_speculation_slots < 0)
      bad_speculation_slots = 0;
    double bad_speculation_pct = (double)bad_speculation_slots / total_slots;

    // Retiring
    double retiring_pct = (double)commit_num / total_slots;

    printf("\033[38;5;34mTotal Slots      : %ld\033[0m\n", total_slots);
    printf("\033[38;5;34mFrontend Bound   : %.2f%%\033[0m\n",
           frontend_bound_pct * 100.0);
    printf("\033[38;5;34m  - Recovery Total : %.2f%%\033[0m\n",
           (double)(slots_frontend_recovery_mispred + slots_frontend_recovery_flush) /
               total_slots * 100.0);
    printf("\033[38;5;34m  - Recovery Mispred: %.2f%%\033[0m\n",
           (double)slots_frontend_recovery_mispred / total_slots * 100.0);
    printf("\033[38;5;34m  - Recovery Flush  : %.2f%%\033[0m\n",
           (double)slots_frontend_recovery_flush / total_slots * 100.0);
    printf("\033[38;5;34m  - Front Pure      : %.2f%%\033[0m\n",
           (double)slots_frontend_pure / total_slots * 100.0);
    printf("\033[38;5;34m  - Fetch Latency  : %.2f%% (Approx by ICache "
           "Miss)\033[0m\n",
           (double)slots_fetch_latency / total_slots * 100.0);
    printf("\033[38;5;34m  - Fetch Bandwidth: %.2f%%\033[0m\n",
           (double)slots_fetch_bandwidth / total_slots * 100.0);
    printf("\033[38;5;34mBackend Bound    : %.2f%%\033[0m\n",
           backend_bound_pct * 100.0);
    printf("\033[38;5;34m  - Memory Bound   : %.2f%% (LSU Stall)\033[0m\n",
           (double)slots_mem_bound_lsu / total_slots * 100.0);
    printf("\033[38;5;34m    - LDQ Full       : %.2f%%\033[0m\n",
           (double)slots_mem_bound_ldq_full / total_slots * 100.0);
    printf("\033[38;5;34m    - STQ Full       : %.2f%%\033[0m\n",
           (double)slots_mem_bound_stq_full / total_slots * 100.0);
    printf("\033[38;5;34m    - L1 Bound       : %.2f%%\033[0m\n",
           (double)slots_mem_l1_bound / total_slots * 100.0);
    printf("\033[38;5;34m    - Ext Memory Bound: %.2f%%\033[0m\n",
           (double)slots_mem_ext_bound / total_slots * 100.0);
    printf("\033[38;5;34m  - Core Bound     : %.2f%% (IQ/ROB Stall)\033[0m\n",
           (double)(slots_core_bound_iq + slots_core_bound_rob) / total_slots *
               100.0);
    printf("\033[38;5;34m    - IQ Bound       : %.2f%%\033[0m\n",
           (double)slots_core_bound_iq / total_slots * 100.0);
    printf("\033[38;5;34m    - ROB Bound      : %.2f%%\033[0m\n",
           (double)slots_core_bound_rob / total_slots * 100.0);
    printf("\033[38;5;34mBad Speculation  : %.2f%%\033[0m\n",
           bad_speculation_pct * 100.0);
    printf("\033[38;5;34mRetiring         : %.2f%%\033[0m\n",
           retiring_pct * 100.0);
    printf("\n");

    // Consume-side L1 (4 buckets):
    // Measure gap between frontend supply capacity and backend acceptance at
    // the InstBuffer consume end (PreIDU->IDU boundary).
    // Labels are prefixed with "IDU " to avoid clashing with current parsers.
    printf(
        "\033[38;5;34m*********Top-Down Analysis (Level 1 - IDU)************\033[0m\n");
    uint64_t idu_total_slots = cycle * DECODE_WIDTH;
    if (idu_total_slots == 0)
      idu_total_slots = 1;

    uint64_t supplied_slots = ib_consume_available_slots;
    if (supplied_slots > idu_total_slots)
      supplied_slots = idu_total_slots;

    uint64_t accepted_slots = ib_consume_consumed_slots;
    if (accepted_slots > supplied_slots)
      accepted_slots = supplied_slots;

    uint64_t retiring_slots = commit_num;
    if (retiring_slots > accepted_slots)
      retiring_slots = accepted_slots;

    uint64_t idu_frontend_bound_slots = idu_total_slots - supplied_slots;
    uint64_t idu_backend_bound_slots = supplied_slots - accepted_slots;
    uint64_t idu_bad_speculation_slots = accepted_slots - retiring_slots;

    double idu_frontend_bound_pct =
        (double)idu_frontend_bound_slots / idu_total_slots;
    double idu_backend_bound_pct =
        (double)idu_backend_bound_slots / idu_total_slots;
    double idu_bad_speculation_pct =
        (double)idu_bad_speculation_slots / idu_total_slots;
    double idu_retiring_pct = (double)retiring_slots / idu_total_slots;

    printf("\033[38;5;34mIDU Total Slots      : %ld\033[0m\n", idu_total_slots);
    printf("\033[38;5;34mIDU Frontend Bound   : %.2f%%\033[0m\n",
           idu_frontend_bound_pct * 100.0);
    printf("\033[38;5;34mIDU Backend Bound    : %.2f%%\033[0m\n",
           idu_backend_bound_pct * 100.0);
    printf("\033[38;5;34mIDU Bad Speculation  : %.2f%%\033[0m\n",
           idu_bad_speculation_pct * 100.0);
    printf("\033[38;5;34mIDU Retiring         : %.2f%%\033[0m\n",
           idu_retiring_pct * 100.0);
    printf("\n");
  }
};
