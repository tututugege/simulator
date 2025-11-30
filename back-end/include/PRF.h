#pragma once
#include "config.h"
#include <EXU.h>
#include <IO.h>

class PRF_OUT {
public:
  Prf_Exe *prf2exe;
  Prf_Rob *prf2rob;
  Prf_Dec *prf2dec;
  Prf_Awake *prf_awake;
};

class PRF_IN {
public:
  Iss_Prf *iss2prf;
  Exe_Prf *exe2prf;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;
};

class PRF {
public:
  PRF_IN in;
  PRF_OUT out;

  void comb_br_check();
  void comb_branch();
  void comb_complete();
  void comb_awake();
  void comb_read();
  void comb_flush();
  void comb_write();
  void comb_pipeline();
  void init();
  void seq();

  // 状态
  reg32_t reg_file[PRF_NUM];
  Inst_entry inst_r[ISSUE_WAY];

  wire32_t reg_file_1[PRF_NUM];
  Inst_entry inst_r_1[ISSUE_WAY];
};
