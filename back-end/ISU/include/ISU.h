#pragma once
#include "config.h"
#include <IO.h>
#include <cstdint>
#include <vector>

class IQ {
public:
  IQ(int entry_num, int out_num, IQ_TYPE);
  void init();
  void wake_up(uint32_t);
  void store_wake_up(bool *);
  void clear();
  vector<Inst_entry> scheduler(Sched_type);
  vector<Inst_entry> deq();
  void enq(Inst_info *inst);
  void dependency(int dest_idx);
  int num;
  int num_temp;

  // config
  int entry_num;
  int out_num;
  IQ_TYPE type;

private:
  vector<Inst_entry> entry;
};

class ISU_IO {
public:
  // rollback
  Rob_Broadcast *rob_bc;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren; // ready

  Iss_Prf *iss2prf;
  Exe_Iss *exe2iss; // br

  Stq_Iss *stq2iss;

  Prf_Awake *awake;
};

class ISU {
public:
  ISU_IO io;
  void init();
  void add_iq(int entry_num, int out_num, IQ_TYPE);
  vector<IQ> iq;
  void comb();
  void seq(); // 写入IQ
  int iq_num = 0;
};
