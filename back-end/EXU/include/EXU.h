#pragma once
#include "IO.h"
#include <FU.h>
#include <config.h>

class EXU_IO {
public:
  Prf_Exe *prf2exe;
  Exe_Iss *exe2iss;

  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;

  Exe_Broadcast *exe_bc;
};

class EXU {
public:
  void init();
  void comb();
  void seq();
  vector<vector<Inst_entry>> inst_r;
  vector<vector<FU>> fu;
  EXU_IO io;
};
