#pragma once
#include "IO.h"
#include <config.h>
#include <cstdint>

class Back_Top;

class STQ_IN {
public:
  Dis_Stq *dis2stq;
  Exe_Stq *exe2stq;
  Rob_Commit *rob_commit;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;
};

class STQ_OUT {
public:
  Stq_Dis *stq2dis;
  Stq_Front *stq2front;
};

typedef struct {
  bool ready[FETCH_WIDTH];

  // 内存写端口
  bool wen;
  uint32_t wdata;
  uint32_t waddr;
  uint32_t wstrb;
} STQ_out;

#define NORMAL false
#define FENCE true

class STQ {
public:
  STQ(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  STQ_IN in;
  STQ_OUT out;
  void comb();
  void comb_fence();
  void st2ld_fwd(uint32_t, uint32_t &, int rob_idx, bool &);
  void seq();

  void init();
  int enq_ptr;
  int deq_ptr;
  bool state = NORMAL;
  STQ_entry entry[STQ_NUM];
  int commit_ptr = 0;
  int count = 0;
  int commit_count = 0;
};
