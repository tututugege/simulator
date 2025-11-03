#pragma once
#include <IO.h>
#include <config.h>

class ROB_IO {
public:
  Dis_Rob *dis2rob;
  Rob_Dis *rob2dis;

  Prf_Rob *prf2rob;

  Rob_Csr *rob2csr;
  Csr_Rob *csr2rob;

  Rob_Commit *rob_commit;

  Rob_Broadcast *rob_bcast;
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
  ROB_IO io;

  // 状态
  Inst_entry entry[ROB_BANK_NUM][ROB_NUM / 4];
  reg7_t enq_ptr;
  reg7_t deq_ptr;
  reg7_t count;
  reg1_t flag;

  Inst_entry entry_1[ROB_BANK_NUM][ROB_NUM / 4];
  wire7_t enq_ptr_1;
  wire7_t deq_ptr_1;
  wire7_t count_1;
  wire1_t flag_1;
};
