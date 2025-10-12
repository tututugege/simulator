#pragma once
#include "CSR.h"
#include "IO.h"
#include <FU.h>
#include <config.h>

class EXU_IO {
public:
  Prf_Exe *prf2exe;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bcast;

  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;
  Exe_Iss *exe2iss;

  Exe_Csr *exe2csr;
  Csr_Exe *csr2exe;
};

class EXU {
public:
  void init();
  void comb_exec();
  void comb_to_csr();
  void comb_from_csr();
  void comb_branch();
  void comb_flush();
  void comb_pipeline();
  void comb_ready();
  void seq();

  // 可以看作一个黑盒电路
  FU fu[ISSUE_WAY];

  // pipeline
  Inst_entry inst_r[ISSUE_WAY];
  Inst_entry inst_r_1[ISSUE_WAY];
  EXU_IO io;
};
