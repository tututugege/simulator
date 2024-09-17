#pragma once
#include <EXU.h>
#include <IQ.h>
#include <LSU.h>
#include <PRF.h>
#include <ROB.h>
#include <Rename.h>
#include <config.h>

typedef struct Back_in {
  Inst_info inst[INST_WAY];
} Back_in;

class Back_Top {
private:
  Rename rename;
  IQ iq;
  PRF prf;
  ALU alu[ALU_NUM];
  BRU bru[BRU_NUM];
  AGU agu[AGU_NUM];
  LDQ ldq;
  STQ stq;
  /*int alu_count = ALU_NUM;*/
  ROB rob;

public:
  Back_in in;
  void Back_comb(bool *input_data, bool *output_data); // 组合逻辑
  void Back_seq(bool *input_data, bool *output_data);  // 时序逻辑
  void init(bool *output_data);
  int alloc_alu();
  void difftest(int pc);
};
