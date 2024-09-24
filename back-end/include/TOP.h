#pragma once
#include <EXU.h>
#include <FIFO.h>
#include <IQ.h>
#include <LSU.h>
#include <ROB.h>
#include <Rename.h>
#include <config.h>

typedef struct Back_in {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
} Back_in;

class Back_Top {
private:
  Rename rename;
  IQ int_iq;
  IQ st_iq;
  IQ ld_iq;
  SRAM<uint32_t> prf = SRAM<uint32_t>(PRF_RD_NUM, PRF_WR_NUM, PRF_NUM, 32);
  ALU alu[ALU_NUM];
  BRU bru[BRU_NUM];
  AGU agu[AGU_NUM];
  LDQ ldq;
  STQ stq;
  ROB rob;

public:
  Back_Top();
  void init();
  Back_in in;
  void Back_comb(bool *input_data, bool *output_data); // 组合逻辑
  void Back_seq(bool *input_data, bool *output_data);  // 时序逻辑

  // debug
  void difftest(int pc);
  void arch_update(int areg_idx, int preg_idx);
};
