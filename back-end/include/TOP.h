#pragma once
#include <CSR.h>
#include <EXU.h>
#include <FIFO.h>
#include <IDU.h>
#include <ISU.h>
#include <LSU.h>
#include <PTAB.h>
#include <ROB.h>
#include <Rename.h>
#include <config.h>
#include <cstdint>

typedef struct {
  uint32_t inst[INST_WAY];
  uint32_t pc[INST_WAY];
  bool valid[INST_WAY];
  uint32_t load_data;
} Back_in;

typedef struct {

  // to front-end
  bool mispred;
  bool stall;
  bool exception;
  bool fire[INST_WAY];
  uint32_t pc;

  // memory
  uint32_t load_addr;
  bool store;
  uint32_t store_addr;
  uint32_t store_data;
  uint32_t store_strb;
} Back_out;

class Back_Top {
private:
  Rename rename;
  SRAM<uint32_t> prf = SRAM<uint32_t>(PRF_RD_NUM, PRF_WR_NUM, PRF_NUM, 32);
  ALU alu[ALU_NUM];
  BRU bru[BRU_NUM];
  AGU agu[AGU_NUM];
  STQ stq;
  ROB rob;
  IDU idu;
  CSRU csru;

public:
  IQ int_iq;
  IQ st_iq;
  IQ ld_iq;

  PTAB ptab;
  Back_in in;
  Back_out out;
  Back_Top();
  void init();
  void Back_comb();
  void Back_seq();
  bool pre_br_check(uint32_t *br_pc);

  // debug
  void difftest(Inst_info inst);
};
