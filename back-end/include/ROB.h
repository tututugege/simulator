#pragma once
#include <IO.h>
#include <config.h>
#include <cstdint>

class ROB_IO {
public:
  Dis_Rob *dis2rob;
  Rob_Dis *rob2dis;
  Prf_Rob *prf2rob;
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
  uint32_t pc[ROB_NUM / 4];
  int enq_ptr;
  int deq_ptr;
  int count;

  Inst_entry entry_1[ROB_BANK_NUM][ROB_NUM / 4];
  uint32_t pc_1[ROB_NUM / 4];
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;
};
