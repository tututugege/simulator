#pragma once
#include "frontend.h"
#include <CSR.h>
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

  // ar
  bool arready;

  // r
  bool rvalid;
  uint32_t rdata;

  // w
  bool wready;

  // b
  bool bvalid;
} Back_in;

typedef struct {
  // to front-end
  bool mispred;
  bool stall;
  bool exception;
  bool fire[FETCH_WIDTH];
  uint32_t redirect_pc;
  Inst_entry commit_entry[COMMIT_WIDTH];

  // ar
  uint32_t arvalid; // out
  uint32_t araddr;  // out

  // r
  bool rready; // out

  // w
  bool wvalid;    // out
  uint32_t waddr; // out
  uint32_t wdata; // out
  uint32_t wstrb; // out

  // b
  bool bready; // out

} Back_out;

class Back_Top {
public:
  IDU idu;
  Rename rename;
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
  bool pre_br_check(uint32_t *br_pc);

  // debug
  void difftest(Inst_info *inst);
};
