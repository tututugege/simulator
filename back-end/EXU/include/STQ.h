#pragma once
#include "IO.h"
#include <config.h>
#include <cstdint>

typedef struct {
  Dis_Stq *dis2stq;
  Stq_Dis *stq2dis;
  Exe_Stq *exe2stq;
  Rob_Commit *rob_commit;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;
} STQ_IO;

typedef struct {
  bool ready[FETCH_WIDTH];

  // 内存写端口
  bool wen;
  uint32_t wdata;
  uint32_t waddr;
  uint32_t wstrb;
} STQ_out;

class STQ {
public:
  STQ_IO io;
  void comb();
  void st2ld_fwd(uint32_t, uint32_t &, int rob_idx);
  void seq();

  void init();
  int enq_ptr;
  int deq_ptr;

  STQ_entry entry[STQ_NUM];
  int commit_ptr = 0;
  int count = 0;
  int commit_count = 0;
};
