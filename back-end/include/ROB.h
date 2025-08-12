#pragma once
#include <IO.h>
#include <config.h>

class ROB_IO {
public:
  Ren_Rob *ren2rob;
  Rob_Ren *rob2ren;

  Prf_Rob *prf2rob;

  Rob_Broadcast *rob_bc;
  Rob_Commit *rob_commit;
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

  Inst_entry entry[ROB_NUM];
  bool complete[ROB_NUM];
  bool exception[ROB_NUM];
  int enq_ptr;
  int deq_ptr;
  int count;

  Inst_entry entry_1[ROB_NUM];
  int complete_1[ROB_NUM];
  bool exception_1[ROB_NUM];
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;

#ifdef CONFIG_DIFFTEST
  bool diff[ROB_NUM];
#endif
};
