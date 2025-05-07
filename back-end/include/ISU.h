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
  void br_clear(uint32_t br_mask);
  Inst_entry pop_oldest(vector<Inst_entry> &valid_entry,
                        vector<int> &valid_idx);

  vector<Inst_entry> scheduler(int ready_num);
  vector<Inst_entry> deq(int ready_num);
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
  Rob_Broadcast *rob_bc;
  Dec_Broadcast *dec_bcast;

  Ren_Iss *ren2iss;
  Iss_Ren *iss2ren; // ready

  Iss_Prf *iss2prf;
  Exe_Iss *exe2iss;

  Stq_Iss *stq2iss;

  Prf_Awake *awake;
};

class ISU {
public:
  ISU_IO io;
  void init();
  void add_iq(int entry_num, int out_num, IQ_TYPE);
  vector<IQ> iq;
  void comb_ready();
  void comb_deq();
  void seq(); // 写入IQ
  int iq_num = 0;
};
