#pragma once
#include <iostream>
#include <cstdio>
#include <cstdint>
#include "config.h"

class PerfCount {
public:
  bool perf_start = false;
  uint64_t cycle = 0;
  uint64_t commit_num = 0;

  uint64_t cache_access_num = 0;
  uint64_t cache_miss_num = 0;

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

  uint64_t isu_entry_stall[IQ_NUM];
  uint64_t isu_raw_stall[IQ_NUM];
  uint64_t isu_ready_num[IQ_NUM];

  void perf_reset() {
    cycle = 0;
    commit_num = 0;
    // cache
    cache_access_num = 0;
    cache_miss_num = 0;
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
  }

  void perf_print() {
    printf("\033[1;32minstruction num: %ld\033[0m\n", commit_num);
    printf("\033[1;32mcycle       num: %ld\033[0m\n", cycle);
    printf("\033[1;32mipc            : %f\033[0m\n",
           (double)commit_num / cycle);
    printf("\n");
    perf_print_cache();
    perf_print_icache();
    perf_print_branch();
  }

  void perf_print_cache() {
    printf("\033[1;32m*********CACHE COUNTER************\033[0m\n");

    printf("\033[1;32mcache accuracy : %f\033[0m\n",
           1 - cache_miss_num / (double)cache_access_num);
    printf("\033[1;32mcache access   : %ld\033[0m\n", cache_access_num);
    printf("\033[1;32mcache hit      : %ld\033[0m\n",
           cache_access_num - cache_miss_num);
    printf("\033[1;32mcache miss     : %ld\033[0m\n", cache_miss_num);
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
};
