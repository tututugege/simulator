#pragma once
#include "IO.h"
#include <config.h>
#include <cstdint>

typedef struct {
  Ren_Stq *ren2stq;
  Stq_Ren *stq2ren;
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

  // 唤醒
  uint32_t st_idx;

  // 当前有效信息
  bool entry_valid[STQ_NUM];
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
};
