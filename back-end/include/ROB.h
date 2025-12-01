#pragma once
#include <IO.h>
#include <config.h>

class ROB_OUT {
public:
  Rob_Dis *rob2dis;
  Rob_Csr *rob2csr;
  Rob_Commit *rob_commit;
  Rob_Broadcast *rob_bcast;
};

class ROB_IN {
public:
  Dis_Rob *dis2rob;
  Prf_Rob *prf2rob;
  Csr_Rob *csr2rob;
  Dec_Broadcast *dec_bcast;
};

class ROB {
public:
  void init();
  void seq();
  void comb_ready();
  void comb_commit();
  void comb_complete();
  void comb_fire();
  void comb_branch();
  void comb_flush();

  ROB_IN in;
  ROB_OUT out;

  // 状态
  Inst_entry entry[ROB_BANK_NUM][ROB_NUM / 4];
  reg5_t enq_ptr;
  reg5_t deq_ptr;
  reg5_t count;
  reg1_t flag;

  Inst_entry entry_1[ROB_BANK_NUM][ROB_NUM / 4];
  wire5_t enq_ptr_1;
  wire5_t deq_ptr_1;
  wire5_t count_1;
  wire1_t flag_1;
};
