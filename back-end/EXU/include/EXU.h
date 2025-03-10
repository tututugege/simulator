#pragma once
#include "CSR.h"
#include "IO.h"
#include <FU.h>
#include <config.h>

class EXU_IO {
public:
  Prf_Exe *prf2exe;
  Dec_Broadcast *id_bc;
  Rob_Broadcast *rob_bc;

  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;
  Exe_Iss *exe2iss;

  Exe_Csr *exe2csr;
  Csr_Exe *csr2exe;
};

class EXU {
public:
  void init();
  void comb();
  void seq();
  Inst_entry inst_r[ISSUE_WAY];
  FU fu[ISSUE_WAY];
  EXU_IO io;
};
