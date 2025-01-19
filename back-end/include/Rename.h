#pragma once
#include "IO.h"
#include "config.h"

class REN_IO {
public:
  Dec_Ren *dec2ren;
  Ren_Dec *ren2dec;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren;

  Exe_Ren *exe2ren;
  Exe_Broadcast *exe_bc;

  Rob_Ren *rob2ren;
  Ren_Rob *ren2rob;

  Rob_Broadcast *rob_bc;
  Rob_Commit *rob_commit;
};

class Rename {
public:
  REN_IO io;

  void init();
  void comb();
  void seq();

  // debug
  void print_reg();
  void print_RAT();
  int arch_RAT[ARF_NUM];

private:
  Inst_entry dec_ren_r[INST_WAY];

  // register
  uint32_t spec_RAT[ARF_NUM];
  uint32_t RAT_checkpoint[MAX_BR_NUM][ARF_NUM];
  bool free_vec[PRF_NUM];
  bool alloc_checkpoint[MAX_BR_NUM][PRF_NUM];
  bool busy_table[PRF_NUM];
  bool spec_alloc[PRF_NUM]; // 处于speculative状态分配的寄存器

  // 内部信号
  uint32_t alloc_reg[INST_WAY];
};
