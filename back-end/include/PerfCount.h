#pragma once
#include "config.h"
#include <cstdint>
#include <cstdio>
#include <iostream>

class PerfCount {
public:
  bool perf_start = false; bool icache_busy = false;
  uint64_t cycle = 0;
  uint64_t commit_num = 0;

  uint64_t dcache_access_num = 0;
  uint64_t dcache_miss_num = 0;

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

  uint64_t len_issued_num = 0;
  uint64_t slots_issued = 0;
  uint64_t slots_backend_bound = 0;
  uint64_t slots_frontend_bound = 0;

  // Level 2 Counters
  uint64_t slots_fetch_latency = 0;
  uint64_t slots_fetch_bandwidth = 0;
  uint64_t slots_mem_bound_lsu = 0;
  uint64_t slots_core_bound_iq = 0;
  uint64_t slots_core_bound_rob = 0;

  // Level 3 Counters
  uint64_t slots_mem_l1_bound = 0;
  uint64_t slots_mem_ext_bound = 0;

  uint64_t isu_entry_stall[IQ_NUM];
  uint64_t isu_raw_stall[IQ_NUM];
  uint64_t isu_ready_num[IQ_NUM];

  void perf_reset() {
    cycle = 0;
    commit_num = 0;
    // dcache
    dcache_access_num = 0;
    dcache_miss_num = 0;
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

    len_issued_num = 0;
    slots_issued = 0;
    slots_backend_bound = 0;
    slots_frontend_bound = 0;

    slots_fetch_latency = 0;
    slots_fetch_bandwidth = 0;
    slots_mem_bound_lsu = 0;
    slots_core_bound_iq = 0;
    slots_core_bound_rob = 0;

    slots_mem_l1_bound = 0;
    slots_mem_ext_bound = 0;
  }

  void perf_print() {
    printf("\033[1;32minstruction num: %ld\033[0m\n", commit_num);
    printf("\033[1;32mcycle       num: %ld\033[0m\n", cycle);
    printf("\033[1;32mipc            : %f\033[0m\n",
           (double)commit_num / cycle);
    printf("\n");
    perf_print_dcache();
    perf_print_icache();
    perf_print_branch();
    perf_print_tma();
  }

  void perf_print_dcache() {
    printf("\033[1;32m*********DCACHE COUNTER************\033[0m\n");

    printf("\033[1;32mdcache accuracy : %f\033[0m\n",
           1 - dcache_miss_num / (double)dcache_access_num);
    printf("\033[1;32mdcache access   : %ld\033[0m\n", dcache_access_num);
    printf("\033[1;32mdcache hit      : %ld\033[0m\n",
           dcache_access_num - dcache_miss_num);
    printf("\033[1;32mdcache miss     : %ld\033[0m\n", dcache_miss_num);
    printf("\n");
  }

  void perf_print_icache() {
    printf("\033[1;32m*********ICACHE COUNTER***********\033[0m\n");

    printf("\033[1;32micache accuracy : %f\033[0m\n",
           1 - icache_miss_num / (double)icache_access_num);
    printf("\033[1;32micache access   : %ld\033[0m\n", icache_access_num);
    printf("\033[1;32micache hit      : %ld\033[0m\n",
           icache_access_num - icache_miss_num);
    printf("\033[1;32micache miss     : %ld\033[0m\n", icache_miss_num);
    printf("\n");
  }

  void perf_print_branch() {
    printf("\033[1;32m*********BPU COUNTER************\033[0m\n");
    printf("\033[1;32mbpu   accuracy : %f\033[0m\n\n",
           1 - (cond_mispred_num + jalr_mispred_num + ret_mispred_num) /
                   (double)(cond_br_num + jalr_br_num + ret_br_num));

    printf("\033[1;32mjalr  accuracy : %f\033[0m\n",
           1 - (jalr_mispred_num) / (double)(jalr_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", jalr_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", jalr_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", jalr_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", jalr_dir_mispred);
    printf("\n");

    printf("\033[1;32mbr    accuracy : %f\033[0m\n",
           1 - (cond_mispred_num) / (double)(cond_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", cond_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", cond_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", cond_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", cond_dir_mispred);
    printf("\n");

    printf("\033[1;32mret    accuracy : %f\033[0m\n",
           1 - (ret_mispred_num) / (double)(ret_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", ret_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", ret_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", ret_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", ret_dir_mispred);
    printf("\n");
    printf("\033[1;32m*********STALL COUNTER************\033[0m\n");
    printf("\033[1;32mrob     stall : %ld\033[0m\n", rob_entry_stall);
    printf("\033[1;32midu br  stall : %ld\033[0m\n", idu_br_stall);
    printf("\033[1;32midu tag stall : %ld\033[0m\n", idu_tag_stall);
    printf("\033[1;32mren reg stall : %ld\033[0m\n", ren_reg_stall);
    printf("\n");
    printf("\033[1;32m*********Isu COUNTER************\033[0m\n");

    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miss     stall : %ld\033[0m\n", isu_entry_stall[i]);
    }
    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miq%d ready  num : %f\033[0m\n", i,
             isu_ready_num[i] / (double)cycle);
    }
    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miq%d raw  num : %ld\033[0m\n", i, isu_raw_stall[i]);
    }
  }
  void perf_print_tma() {
    printf(
        "\033[1;32m*********Top-Down Analysis (Level 1)************\033[0m\n");

    // Total slots available
    uint64_t total_slots =
        slots_issued + slots_backend_bound + slots_frontend_bound;
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

    printf("\033[1;32mTotal Slots      : %ld\033[0m\n", total_slots);
    printf("\033[1;32mFrontend Bound   : %.2f%%\033[0m\n",
           frontend_bound_pct * 100.0);
    printf("\033[1;32m  - Fetch Latency  : %.2f%% (Approx by ICache "
           "Miss)\033[0m\n",
           (double)slots_fetch_latency / total_slots * 100.0);
    printf("\033[1;32m  - Fetch Bandwidth: %.2f%%\033[0m\n",
           (double)slots_fetch_bandwidth / total_slots * 100.0);
    printf("\033[1;32mBackend Bound    : %.2f%%\033[0m\n",
           backend_bound_pct * 100.0);
    printf("\033[1;32m  - Memory Bound   : %.2f%% (LSU Stall)\033[0m\n",
           (double)slots_mem_bound_lsu / total_slots * 100.0);
    printf("\033[1;32m    - L1 Bound       : %.2f%%\033[0m\n",
           (double)slots_mem_l1_bound / total_slots * 100.0);
    printf("\033[1;32m    - Ext Memory Bound: %.2f%%\033[0m\n",
           (double)slots_mem_ext_bound / total_slots * 100.0);
    printf("\033[1;32m  - Core Bound     : %.2f%% (IQ/ROB Stall)\033[0m\n",
           (double)(slots_core_bound_iq + slots_core_bound_rob) / total_slots *
               100.0);
    printf("\033[1;32mBad Speculation  : %.2f%%\033[0m\n",
           bad_speculation_pct * 100.0);
    printf("\033[1;32mRetiring         : %.2f%%\033[0m\n",
           retiring_pct * 100.0);
    printf("\n");
  }
};
