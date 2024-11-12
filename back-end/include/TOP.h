#pragma once
#include <EXU.h>
#include <FIFO.h>
#include <IDU.h>
#include <ISU.h>
#include <LSU.h>
#include <PTAB.h>
#include <ROB.h>
#include <Rename.h>
#include <config.h>

class Back_Top {
private:
  Rename rename;
  IQ int_iq;
  IQ st_iq;
  IQ ld_iq;
  SRAM<uint32_t> prf = SRAM<uint32_t>(PRF_RD_NUM, PRF_WR_NUM, PRF_NUM, 32);
  PTAB ptab;
  ALU alu[ALU_NUM];
  BRU bru[BRU_NUM];
  AGU agu[AGU_NUM];
  STQ stq;
  ROB rob;
  IDU idu;

public:
  Back_Top();
  void init();
  void Back_comb(bool *input_data, bool *output_data);
  void Back_seq();

  // debug
  void difftest(Inst_info inst);
};
