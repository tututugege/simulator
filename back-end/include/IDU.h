#pragma once
#include "IO.h"
#include "config.h"

class IDU_IO {
public:
  Front_Dec *front2id;
  Dec_Front *id2front;

  Dec_Ren *id2ren;
  Ren_Dec *ren2id;

  Dec_Broadcast *id_bc;

  Prf_Dec *prf2id;
  Rob_Broadcast *rob_bc;
  Rob_Commit *commit;
};

class IDU {

public:
  int in_size = 304;
  int out_size = 445;
  int reg_size = 41;
  bool input[304 + 41];
  bool output[445 + 41];

  void init();
  void comb();
  void seq();
  void io_gen();
  void reg_gen();
  IDU_IO io;
  bool pipeline = true;

private:
  bool tag_vec[MAX_BR_NUM];
  uint32_t tag_fifo[MAX_BR_NUM];
  int enq_ptr = 0;
  int deq_ptr = 0;
  int now_tag;
  int alloc_tag;
};
