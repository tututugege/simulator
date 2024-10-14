#pragma once
#include "config.h"

typedef struct Rename_out {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
  bool ready[INST_WAY];
} Rename_out;

typedef struct Rename_in {
  // rename
  bool dis_fire[INST_WAY];

  bool valid[INST_WAY];
  Inst_info inst[INST_WAY];

  // commit 更新arch RAT
  Inst_info commit_inst[ISSUE_WAY];
  bool commit_valid[ISSUE_WAY];

  // 分支信息
  Br_info br;
} Rename_in;

class Rename {
public:
  Rename_in in;
  Rename_out out;
  void init();
  void comb_alloc();
  void comb_fire();
  void comb_2();
  void seq(); // 时序逻辑

  // debug
  void print_reg();
  void print_RAT();
  int arch_RAT[ARF_NUM];

private:
  // register
  uint32_t spec_RAT[ARF_NUM];
  uint32_t RAT_checkpoint[MAX_BR_NUM][ARF_NUM];
  bool free_vec[PRF_NUM];
  bool alloc_checkpoint[MAX_BR_NUM][PRF_NUM];
  bool busy_table[PRF_NUM];

  uint32_t spec_RAT_1[ARF_NUM];
  uint32_t RAT_checkpoint_1[MAX_BR_NUM][ARF_NUM];
  bool free_vec_1[PRF_NUM];
  bool alloc_checkpoint_1[MAX_BR_NUM][PRF_NUM];
  bool busy_table_1[PRF_NUM];

  // 内部信号
  uint32_t alloc_reg[INST_WAY];
  bool done[INST_WAY];
};
