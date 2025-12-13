#pragma once
#include "config.h"
#include <IO.h>
#include <cstdint>
#include <vector>
#define FORCE_INLINE __attribute__((always_inline)) inline

class IQ {
public:
  IQ(int, int);
  void init();
  void FORCE_INLINE wake_up(bool *valid, uint32_t *preg);
  Inst_entry FORCE_INLINE scheduler();
  void latency_wake();
  void sta_wake_up(int);
  void std_wake_up(int);
  void br_clear(uint32_t br_mask);
  void enq(Inst_uop &inst);
  Inst_entry deq();
  int entry_num;
  int type;

  vector<Inst_entry> entry;
  int num; // 电路中不一定要有这个东西，主要用于debug

  vector<Inst_entry> entry_1;
  int num_1;
};

class ISU_OUT {
public:
  Iss_Prf *iss2prf;
  Iss_Dis *iss2dis;
  Iss_Awake *iss_awake;
};

class ISU_IN {
public:
  Dis_Iss *dis2iss;
  Prf_Awake *prf_awake;
  Exe_Iss *exe2iss;
  Rob_Broadcast *rob_bcast;
  Dec_Broadcast *dec_bcast;
};

class ISU {
public:
  ISU_IN in;
  ISU_OUT out;
  void init();
  void add_iq(int entry_num, int);
  void comb_ready();
  void comb_deq();
  void comb_enq();
  void comb_flush();
  void comb_branch();
  void comb_awake();
  void seq();

  vector<IQ> iq; // xxx_1在这里面
};
