#pragma once
#include "IO.h"
#include <FU.h>
#include <config.h>

class Back_Top;

class EXU_IN {
public:
  Prf_Exe *prf2exe;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;

  Csr_Exe *csr2exe;

#ifdef CONFIG_CACHE
  Mem_READY *cache2exe_ready;
#endif
};

class EXU_OUT {
public:
  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;
  Exe_Iss *exe2iss;

  Exe_Csr *exe2csr;
#ifdef CONFIG_CACHE
  Mem_REQ *exe2cache;
  Dcache_CONTROL *exe2cache_control;
#endif
};
class EXU {
public:
  EXU(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  void init();
  void comb_exec();
  void comb_to_csr();
  void comb_from_csr();
  void comb_branch();
  void comb_flush();
  void comb_pipeline();
  void comb_ready();
  void seq();
#ifdef CONFIG_CACHE
  void comb_latency();
#endif
  EXU_IN in;
  EXU_OUT out;

  // 可以看作一个黑盒电路
  FU fu[ISSUE_WAY];

  // pipeline
  Inst_entry inst_r[ISSUE_WAY];
  Inst_entry inst_r_1[ISSUE_WAY];
};
