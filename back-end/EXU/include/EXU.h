#pragma once
#include "CSR.h"
#include "IO.h"
#include <FU.h>
#include <config.h>
#include <vector>

class EXU_IO {
public:
  Iss_Exe *iss2exe;
  Prf_Exe *prf2exe;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bc;

  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;
  Exe_Iss *exe2iss;

  Exe_Csr *exe2csr;
  Csr_Exe *csr2exe;
};

class EXU {
public:
  void default_val();
  void comb_exec();
  void comb_csr();
  void comb_iss_rdy();
  void comb_wb();
  void seq();
  FU fu[ISSUE_WAY];
  Inst_entry inst_r[ISSUE_WAY];
  EXU_IO io;
};
