#pragma once
#include "config.h"
typedef struct Rename_out {
  Inst_info inst[INST_WAY];

  bool valid[INST_WAY]; // to dispatch
  bool ready[INST_WAY]; // to_decode
  bool all_ready;
} Rename_out;

typedef struct Rename_in {
  // rename
  bool valid[INST_WAY];          // from decode
  bool from_dis_ready[INST_WAY]; // from dispatch
  bool from_dis_all_ready;
  bool from_rob_ready[INST_WAY]; // from dispatch
  bool from_rob_all_ready;

  Inst_info inst[INST_WAY];

  // commit 更新arch RAT
  Inst_info commit_inst[INST_WAY];
  bool commit_valid[ISSUE_WAY];

  // 分支信息
  Br_info br;
} Rename_in;

class Rename {
public:
  Rename_in in;
  Rename_out out;
  void init();
  void comb_0();
  void comb_1();
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
};
