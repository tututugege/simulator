#pragma once
#include "config.h"
typedef struct RAT_out {
  int src1_preg_idx[WAY];
  int src2_preg_idx[WAY];
  int dest_preg_idx[WAY];
} RAT_out;

typedef struct RAT_in {
  int src1_areg_idx[WAY];
  int src1_areg_en[WAY];
  int src2_areg_idx[WAY];
  int src2_areg_en[WAY];
  int dest_areg_idx[WAY];
  int dest_areg_en[WAY];
} RAT_in;

class Rename {
public:
  void init();
  int alloc_reg();
  void free_reg(int idx);
  void cycle();
  RAT_in in;
  RAT_out out;

private:
  int spec_RAT[ARF_NUM];
  int arch_RAT[ARF_NUM];
  int free_list[PRF_NUM];
  int free_list_head = 0;
  int free_list_tail = ARF_NUM;
  int free_list_count = ARF_NUM;
};
