#pragma once
#include "config.h"
#include <cstdint>
#include <cstdio>
#include <iostream>

class PerfCount {
public:
  bool perf_start = false;
  bool icache_busy = false;
  uint64_t cycle = 0;
  uint64_t commit_num = 0;

  uint64_t dcache_access_num = 0;
  uint64_t dcache_miss_num = 0;
  uint64_t dcache_l2_access_num = 0;
  uint64_t dcache_l2_miss_num = 0;

  uint64_t icache_access_num = 0;
  uint64_t icache_miss_num = 0;

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

  uint64_t rob_entry_stall = 0;
  uint64_t idu_br_stall = 0;
  uint64_t ren_reg_stall = 0;
  uint64_t idu_tag_stall = 0;
  uint64_t stall_br_id_cycles = 0;
  uint64_t stall_preg_cycles = 0;
  uint64_t stall_rob_full_cycles = 0;
  uint64_t stall_iq_full_cycles = 0;
  uint64_t stall_ldq_full_cycles = 0;
  uint64_t stall_stq_full_cycles = 0;

  uint64_t len_issued_num = 0;
  uint64_t slots_issued = 0;
  uint64_t slots_backend_bound = 0;
  uint64_t slots_frontend_bound = 0;

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

  uint64_t isu_entry_stall[IQ_NUM];
  uint64_t isu_raw_stall[IQ_NUM];
  uint64_t isu_ready_num[IQ_NUM];

  // Squashed valid instructions on the IDU->Dispatch path
  uint64_t squash_flush_total = 0;
  uint64_t squash_mispred_total = 0;
  uint64_t squash_flush_idu = 0;
  uint64_t squash_mispred_idu = 0;
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
    // dcache
    dcache_access_num = 0;
    dcache_miss_num = 0;
    dcache_l2_access_num = 0;
    dcache_l2_miss_num = 0;
    icache_access_num = 0;
    icache_miss_num = 0;

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
    rob_entry_stall = 0;
    idu_br_stall = 0;
    ren_reg_stall = 0;
    idu_tag_stall = 0;
    stall_br_id_cycles = 0;
    stall_preg_cycles = 0;
    stall_rob_full_cycles = 0;
    stall_iq_full_cycles = 0;
    stall_ldq_full_cycles = 0;
    stall_stq_full_cycles = 0;

    len_issued_num = 0;
    slots_issued = 0;
    slots_backend_bound = 0;
    slots_frontend_bound = 0;

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
    squash_flush_idu = 0;
    squash_mispred_idu = 0;
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
    front_predecode_gate_block_fifo_empty_cycle_total = 0;
    front_predecode_gate_block_ptab_empty_cycle_total = 0;
    front_predecode_gate_block_reset_refetch_cycle_total = 0;
    ib_write_inst_total = 0;
    ib_write_cycle_total = 0;
    ib_blocked_cycles = 0;
    ftq_blocked_cycles = 0;
  }

  void perf_print() {
    printf("\033[38;5;34minstruction num: %ld\033[0m\n", commit_num);
    printf("\033[38;5;34mcycle       num: %ld\033[0m\n", cycle);
    printf("\033[38;5;34mipc            : %f\033[0m\n",
           (double)commit_num / cycle);
    printf("\n");
    perf_print_dcache();
    perf_print_icache();
    perf_print_ptw();
    perf_print_branch();
    perf_print_frontend_fetch();
    perf_print_resource_stall();
    perf_print_tma();
  }

  void perf_print_dcache() {
    printf("\033[38;5;34m*********DCACHE COUNTER************\033[0m\n");

    printf("\033[38;5;34mdcache accuracy : %f\033[0m\n",
           1 - dcache_miss_num / (double)dcache_access_num);
    printf("\033[38;5;34mdcache access   : %ld\033[0m\n", dcache_access_num);
    printf("\033[38;5;34mdcache hit      : %ld\033[0m\n",
           dcache_access_num - dcache_miss_num);
    printf("\033[38;5;34mdcache miss     : %ld\033[0m\n", dcache_miss_num);
    if (DCACHE_L2_ENABLE) {
      double l2_acc = (dcache_l2_access_num == 0)
                          ? 1.0
                          : 1.0 -
                                dcache_l2_miss_num /
                                    static_cast<double>(dcache_l2_access_num);
      printf("\033[38;5;34mdcache l2 acc   : %f\033[0m\n", l2_acc);
      printf("\033[38;5;34mdcache l2 access: %ld\033[0m\n",
             dcache_l2_access_num);
      printf("\033[38;5;34mdcache l2 miss  : %ld\033[0m\n",
             dcache_l2_miss_num);
    }
    printf("\n");
  }

  void perf_print_icache() {
    printf("\033[38;5;34m*********ICACHE COUNTER***********\033[0m\n");

    printf("\033[38;5;34micache accuracy : %f\033[0m\n",
           1 - icache_miss_num / (double)icache_access_num);
    printf("\033[38;5;34micache access   : %ld\033[0m\n", icache_access_num);
    printf("\033[38;5;34micache hit      : %ld\033[0m\n",
           icache_access_num - icache_miss_num);
    printf("\033[38;5;34micache miss     : %ld\033[0m\n", icache_miss_num);
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
    // printf("\033[38;5;34m*********STALL COUNTER************\033[0m\n");
    // printf("\033[38;5;34mrob     stall : %ld\033[0m\n", rob_entry_stall);
    // printf("\033[38;5;34midu br  stall : %ld\033[0m\n", idu_br_stall);
    // idu tag stall print removed on request.
    // printf("\033[38;5;34mren reg stall : %ld\033[0m\n", ren_reg_stall);
    // printf("\n");
    // printf("\033[38;5;34m*********Isu COUNTER************\033[0m\n");
    //
    // for (int i = 0; i < IQ_NUM; i++) {
    //   printf("\033[38;5;34miss     stall : %ld\033[0m\n", isu_entry_stall[i]);
    // }
    // for (int i = 0; i < IQ_NUM; i++) {
    //   printf("\033[38;5;34miq%d ready  num : %f\033[0m\n", i,
    //          isu_ready_num[i] / (double)cycle);
    // }
    // for (int i = 0; i < IQ_NUM; i++) {
    //   printf("\033[38;5;34miq%d raw  num : %ld\033[0m\n", i, isu_raw_stall[i]);
    // }
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

  void perf_print_squash() {}

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
  }
};
