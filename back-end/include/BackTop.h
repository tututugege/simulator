#pragma once
#include "IO.h"
#include <Csr.h>
#include <Dispatch.h>
#include <Exu.h>
#include <FTQ.h>
#include <Idu.h>
#include <Isu.h>
#include <Prf.h>
#include <Ren.h>
#include <Rob.h>
#include <config.h>
#include <cstdint>

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
  bool fence_i;
  bool fire[FETCH_WIDTH];
  uint32_t redirect_pc;
  InstEntry commit_entry[COMMIT_WIDTH];

  wire<32> sstatus;
  wire<32> mstatus;
  wire<32> satp;
  wire<2> privilege;
} Back_out;

class Dispatch;
class Idu;
class Ren;
class Isu;
class Prf;
class Exu;
class Rob;
class Csr;
class AbstractLsu;
class AbstractMMU;

class BackTop {
private:
  FrontDecIO front2dec;
  DecFrontIO dec2front;

  DecRenIO dec2ren;
  DecBroadcastIO dec_bcast;

  RenDecIO ren2dec;
  RenDisIO ren2dis;

  DisRenIO dis2ren;
  DisIssIO dis2iss;
  DisRobIO dis2rob;
  DisLsuIO dis2lsu;

  IssAwakeIO iss_awake;
  IssPrfIO iss2prf;
  IssDisIO iss2dis;

  PrfExeIO prf2exe;
  PrfRobIO prf2rob;
  PrfAwakeIO prf_awake;
  PrfDecIO prf2dec;

  ExePrfIO exe2prf;
  ExeIssIO exe2iss;
  ExeLsuIO exe2lsu;

  LsuExeIO lsu2exe;
  LsuDisIO lsu2dis;
  LsuRobIO lsu2rob;

  RobDisIO rob2dis;
  RobCsrIO rob2csr;
  RobBroadcastIO rob_bcast;
  RobCommitIO rob_commit;

  CsrExeIO csr2exe;
  CsrRobIO csr2rob;
  CsrFrontIO csr2front;
  CsrStatusIO csr_status;
  ExeCsrIO exe2csr;

public:
  SimContext *ctx;

  Idu *idu;
  Ren *rename;
  Dispatch *dis;
  Isu *isu;
  Prf *prf;
  Exu *exu;
  Csr *csr;
  Rob *rob;
  AbstractLsu *lsu;
  FTQ *ftq;

  Back_in in;
  Back_out out;
  void init();
  void comb_csr_status();
  void comb();
  void seq();

  BackTop(SimContext *ctx) { this->ctx = ctx; };
  ~BackTop() {
    delete rename;
    delete dis;
    delete idu;
    delete isu;
    delete exu;
    delete prf;
    delete rob;
    delete csr;
    delete ftq;
  }

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
  void difftest_inst(InstEntry *inst_entry);
  void difftest_cycle();
  void difftest_sync(InstInfo *inst);
  uint32_t get_reg(uint8_t arch_idx) {
    return prf->reg_file[rename->arch_RAT_1[arch_idx]];
  }
};
