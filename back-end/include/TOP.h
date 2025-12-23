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
#include <fstream>

class SimContext;

typedef struct {
  uint32_t inst[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  uint32_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
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

  wire32_t sstatus;
  wire32_t mstatus;
  wire32_t satp;
  wire2_t privilege;

} Back_out;

class Back_Top {
public:
  SimContext *ctx;
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
  void comb_csr_status();
  void comb();
  void seq();

  Back_Top(SimContext *ctx)
      : idu(ctx), rename(ctx), dis(ctx), isu(ctx), prf(ctx), stq(ctx), rob(ctx),
        exu(ctx) {
    this->ctx = ctx;
  };

  uint32_t number_PC = 0;

#ifdef CONFIG_MMU
  bool load_data(uint32_t &data, uint32_t v_addr, int rob_idx,
                 bool &mmu_page_fault, uint32_t &mmu_ppn, bool &stall_load);
#else
  bool load_data(uint32_t &data, uint32_t v_addr, int rob_idx);
#endif

  void load_image(const std::string &filename);
  void restore_checkpoint(const std::string &filename);
  void restore_from_ref();

  // debug
  void difftest_inst(Inst_uop *inst);
  void difftest_cycle();
};
