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
  void comb();
  ROB_IO io;

private:
  Inst_entry entry[ROB_NUM];
  bool complete[ROB_NUM];
  bool exception[ROB_NUM];
  int enq_ptr;
  int deq_ptr;
  int count;

#ifdef CONFIG_DIFFTEST
  bool diff[ROB_NUM];
#endif
};
