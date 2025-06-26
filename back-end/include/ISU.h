#pragma once
#include "config.h"
#include <IO.h>
#include <cstdint>
#include <vector>

class IQ {
public:
  IQ(int entry_num, IQ_TYPE);
  void wake_up(uint32_t);
  void store_wake_up(int);
  void br_clear(uint32_t br_mask);
  Inst_entry pop_oldest(vector<Inst_entry> &valid_entry,
                        vector<int> &valid_idx);

  Inst_entry scheduler();
  Inst_entry deq();
  void enq(Inst_uop *inst);
  void dependency(int dest_idx);

  int num;
  int num_temp;

  // config
  int entry_num;
  IQ_TYPE type;

private:
  vector<Inst_entry> entry;
};

class ISU_IO {
public:
  Rob_Broadcast *rob_bc;
  Dec_Broadcast *dec_bcast;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren; // ready

  Iss_Prf *iss2prf;
  Exe_Iss *exe2iss;
  Iss_Exe *iss2exe;

  Stq_Iss *stq2iss;

  Prf_Awake *awake;
};

class ISU {
public:
  ISU_IO io;
  void init();
  void default_val();
  void add_iq(int entry_num, IQ_TYPE);
  vector<IQ> iq;
  void comb_ren_rdy();
  void comb_fire();
  void comb_wake();
  void comb_deq();
  void seq(); // 写入IQ
  int iq_num = 0;
};
