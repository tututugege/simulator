#pragma once
#include "config.h"
typedef struct Rename_out {
  int src1_preg_idx[WAY];
  int src2_preg_idx[WAY];
  int dest_preg_idx[WAY];
  int old_dest_preg_idx[WAY];
  bool src1_raw[WAY];
  bool src2_raw[WAY];
} Rename_out;

typedef struct Rename_in {
  int src1_areg_idx[WAY];
  int src1_areg_en[WAY];
  int src2_areg_idx[WAY];
  int src2_areg_en[WAY];
  int dest_areg_idx[WAY];
  int dest_areg_en[WAY];
} Rename_in;

class Rename {
public:
  void init();

  int alloc_reg();
  void free_reg(int idx);
  void print_reg();
  void print_RAT();
  uint32_t reg(int idx);

  void cycle();
  void recover(); // 将arch_RAT 复制到 spec_RAT 用于分支预测错误时的恢复

  Rename_in in;
  Rename_out out;
  int arch_RAT[ARF_NUM];
  bool *preg_base;

private:
  int spec_RAT[ARF_NUM];
  int free_list[PRF_NUM];
  int free_list_head = 0;
  int free_list_tail = ARF_NUM;
  int free_list_count = ARF_NUM;
};
