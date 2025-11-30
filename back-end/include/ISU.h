#pragma once
#include "config.h"
#include <IO.h>
#include <cstdint>
#include <vector>

class IQ {
public:
  IQ(int, int);
  void init();
  void wake_up(uint32_t, uint32_t latency);
  void latency_wake();
  void sta_wake_up(int);
  void std_wake_up(int);
  void br_clear(uint32_t br_mask);
  Inst_entry scheduler();
  Inst_entry deq();
  void enq(Inst_uop &inst);
  int entry_num;
  int type;

  vector<Inst_entry> entry;
  int num; // 电路中不一定要有这个东西

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
