#pragma once
#include "IO.h"
#include "config.h"

class REN_IO {
public:
  Dec_Ren *dec2ren;
  Ren_Dec *ren2dec;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren;

  Dec_Broadcast *dec_bcast;
  Prf_Awake *awake;

  Rob_Ren *rob2ren;
  Ren_Rob *ren2rob;

  Ren_Stq *ren2stq;
  Stq_Ren *stq2ren;

  Rob_Broadcast *rob_bc;
  Rob_Commit *rob_commit;
};

class Rename {
public:
  REN_IO io;

  void init();
  void comb_rename(); // 重命名
  void comb_fire();
  void comb_wake();
  void comb_alloc(); // 分配寄存器
  void comb_store();
  void comb_branch();
  void comb_commit();
  void comb_pipeline();
  void seq();

  // debug
  void print_reg();
  void print_RAT();
  int arch_RAT[ARF_NUM];

private:
  // register
  Inst_entry inst_r[FETCH_WIDTH];
  uint32_t spec_RAT[ARF_NUM];
  uint32_t RAT_checkpoint[MAX_BR_NUM][ARF_NUM];
  bool free_vec[PRF_NUM];
  bool alloc_checkpoint[MAX_BR_NUM][PRF_NUM];
  bool busy_table[PRF_NUM];
  bool spec_alloc[PRF_NUM]; // 处于speculative状态分配的寄存器

  Inst_entry inst_r_1[FETCH_WIDTH];
  uint32_t spec_RAT_1[ARF_NUM];
  uint32_t RAT_checkpoint_1[MAX_BR_NUM][ARF_NUM];
  bool free_vec_1[PRF_NUM];
  bool alloc_checkpoint_1[MAX_BR_NUM][PRF_NUM];
  bool busy_table_1[PRF_NUM];
  bool spec_alloc_1[PRF_NUM]; // 处于speculative状态分配的寄存器
};
