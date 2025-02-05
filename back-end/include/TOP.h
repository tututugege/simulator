#pragma once
#include <CSR.h>
#include <EXU.h>
#include <IDU.h>
#include <ISU.h>
#include <PRF.h>
#include <PTAB.h>
#include <ROB.h>
#include <Rename.h>
#include <STQ.h>
#include <config.h>
#include <cstdint>

typedef struct {
  uint32_t inst[INST_WAY];
  uint32_t pc[INST_WAY];
  bool valid[INST_WAY];
  uint32_t load_data;

  // ar
  bool arready;

  // r
  bool rvalid;
  uint32_t rdata;

  // aw
  bool awready;

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
  bool fire[INST_WAY];
  uint32_t pc;

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
private:
  IDU idu;
  Rename rename;
  ISU isu;
  PRF prf;
  EXU exu;
  CSRU csru;
  STQ stq;
  ROB rob;

public:
  PTAB ptab;
  Back_in in;
  Back_out out;
  void init();
  void Back_comb();
  void Back_seq();
  bool pre_br_check(uint32_t *br_pc);

  // debug
  void difftest(Inst_info *inst);
};
