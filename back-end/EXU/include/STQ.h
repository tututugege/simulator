#pragma once
#include "IO.h"
#include <config.h>
#include <cstdint>

class STQ_IN {
public:
  Dis_Stq *dis2stq;
  Exe_Stq *exe2stq;
  Rob_Commit *rob_commit;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;

  #ifdef CONFIG_CACHE_MMU
  Mem_RESP * cache2stq;
  Mem_READY * cache2stq_ready;
  #endif
};

class STQ_OUT {
public:
  Stq_Dis *stq2dis;
  #ifdef CONFIG_CACHE_MMU
  Mem_REQ * stq2cache_req;
  #endif
};

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
  STQ_IN in;
  STQ_OUT out;
  #ifndef CONFIG_CACHE_MMU
  void comb();
  void st2ld_fwd(uint32_t, uint32_t &, int rob_idx, bool &);
  #else
  void comb_out();
  void comb_in();
  void st2ld_fwd(uint32_t, uint32_t &, int rob_idx);
  int deq_num;
  int fwd_ptr = 0;
  int write_flag = 0;
  #endif
  void seq();

  void init();
  int enq_ptr;
  int deq_ptr;

  STQ_entry entry[STQ_NUM];
  int commit_ptr = 0;
  int count = 0;
  int commit_count = 0;
};
