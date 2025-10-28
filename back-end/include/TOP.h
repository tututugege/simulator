#pragma once
#include <CSR.h>
#include <Dispatch.h>
#include <EXU.h>
#include <IDU.h>
#include <ISU.h>
#include <PRF.h>
#include <ROB.h>
#include <Rename.h>
#include <STQ.h>
#include <config.h>
#include <cstdint>

typedef struct {
  uint32_t inst[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  uint32_t predict_next_fetch_address[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
} Back_in;

typedef struct {
  // to front-end
  bool mispred;
  bool stall;
  bool flush;
  bool fire[FETCH_WIDTH];
  uint32_t redirect_pc;
  Inst_entry commit_entry[COMMIT_WIDTH];
} Back_out;

class Back_Top {
public:
  IDU idu;
  Rename rename;
  Dispatch dis;
  ISU isu;
  PRF prf;
  EXU exu;
  CSRU csr;
  STQ stq;
  ROB rob;

  Back_in in;
  Back_out out;
  void init();
  void Back_comb();
  void Back_seq();

  // debug
  void difftest(Inst_uop *inst);
};
