#pragma once
#include "config.h"
#include <IO.h>
#include <cstdint>
#include <vector>

class IQ {
public:
  IQ(int entry_num, IQ_TYPE);
  void init();
  void wake_up(uint32_t);
  void sta_wake_up(int);
  void std_wake_up(int);
  void br_clear(uint32_t br_mask);
  Inst_entry scheduler();
  Inst_entry deq();
  void enq(Inst_uop &inst);

  int num;
  int num_temp;

  // config
  int entry_num;
  IQ_TYPE type;

  vector<Inst_entry> entry;
};

class ISU_IO {
public:
  Rob_Broadcast *rob_bcast;
  Dec_Broadcast *dec_bcast;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren; // ready

  Iss_Prf *iss2prf;
  Exe_Iss *exe2iss;

  Prf_Awake *awake;
};

class ISU {
public:
  ISU_IO io;
  void init();
  void add_iq(int entry_num, IQ_TYPE);
  void comb_ready();
  void comb_deq();
  void seq(); // 写入IQ

  vector<IQ> iq;
};
