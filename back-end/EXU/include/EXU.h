#pragma once
#include "CSR.h"
#include "IO.h"
#include <FU.h>
#include <config.h>
#include <vector>

class EXU_IO {
public:
  Prf_Exe *prf2exe;
  Dec_Broadcast *dec_bcast;
  Rob_Broadcast *rob_bc;

  Exe_Prf *exe2prf;
  Exe_Stq *exe2stq;
  Exe_Iss *exe2iss;

  Exe_Csr *exe2csr;
  Csr_Exe *csr2exe;
};

/*class Fu_Group {*/
/*public:*/
/*  Fu_Group(IQ_TYPE, int fu_num, int write_port_num);*/
/*  void exec(vector<Inst_entry> iss_entry);*/
/*  vector<Inst_entry> inst_r;*/
/*  vector<FU> fu;*/
/*  IQ_TYPE type;*/
/*  int fu_num;*/
/*  int write_port_num;*/
/*};*/

class EXU {
public:
  void init();
  void comb_exec();
  void comb_csr();
  void comb_ready();
  void seq();
  FU fu[ISSUE_WAY];
  Inst_entry inst_r[ISSUE_WAY];
  EXU_IO io;
};
