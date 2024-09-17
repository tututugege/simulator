#pragma once
#include "config.h"
typedef struct Rename_out {
  int src1_preg_idx[INST_WAY];
  int src2_preg_idx[INST_WAY];
  int dest_preg_idx[INST_WAY];
  int old_dest_preg_idx[INST_WAY];
  bool src1_raw[INST_WAY];
  bool src2_raw[INST_WAY];
  bool full;
} Rename_out;

typedef struct Rename_in {
  // rename
  int src1_areg_idx[INST_WAY];
  int src1_areg_en[INST_WAY];
  int src2_areg_idx[INST_WAY];
  int src2_areg_en[INST_WAY];
  int dest_areg_idx[INST_WAY];
  int dest_areg_en[INST_WAY];

  // commit 更新arch RAT
  int commit_dest_en[ISSUE_WAY];
  int commit_dest_preg_idx[ISSUE_WAY];
  int commit_dest_areg_idx[ISSUE_WAY];
  int commit_old_dest_areg_idx[ISSUE_WAY];
} Rename_in;

class Rename {
public:
  void init();

  int alloc_reg();
  void free_reg(int idx);
  void print_reg();
  void print_RAT();
  uint32_t reg(int idx);

  void seq();
  void comb();
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
