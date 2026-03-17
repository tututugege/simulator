#pragma once
#include "Csr.h"
#include "Dispatch.h"
#include "Exu.h"
#include "FTQ.h"
#include "IO.h"
#include "Idu.h"
#include "Isu.h"
#include "PreIduQueue.h"
#include "Prf.h"
#include "Ren.h"
#include "Rob.h"
#include "config.h"
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
  uint32_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  bool sc_used[FETCH_WIDTH];
  bool sc_pred[FETCH_WIDTH];
  int16_t sc_sum[FETCH_WIDTH];
  uint16_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  bool loop_used[FETCH_WIDTH];
  bool loop_hit[FETCH_WIDTH];
  bool loop_pred[FETCH_WIDTH];
  uint16_t loop_idx[FETCH_WIDTH];
  uint16_t loop_tag[FETCH_WIDTH];
  uint32_t predict_next_fetch_address[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
} Back_in;

typedef struct {
  // to front-end
  bool mispred;
  bool stall;
  bool flush;
  bool fence_i;
  bool itlb_flush;
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
class MemSubsystem;

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
  PrfAwakeIO prf_awake;
  ExuIdIO exu2id;
  ExuRobIO exu2rob;
  FtqExuPcReqIO ftq_exu_pc_req;
  FtqExuPcRespIO ftq_exu_pc_resp;

  ExePrfIO exe2prf;
  ExeIssIO exe2iss;
  ExeLsuIO exe2lsu;

  LsuExeIO lsu2exe;
  LsuDisIO lsu2dis;
  LsuRobIO lsu2rob;
  MemReqIO lsu2dcache_req;
  MemReqIO lsu2dcache_wreq;
  MemRespIO dcache2lsu_resp;
  MemReadyIO dcache2lsu_wready;

  RobDisIO rob2dis;
  RobCsrIO rob2csr;
  RobBroadcastIO rob_bcast;
  RobCommitIO rob_commit;
  FtqRobPcReqIO ftq_rob_pc_req;
  FtqRobPcRespIO ftq_rob_pc_resp;

  CsrExeIO csr2exe;
  CsrRobIO csr2rob;
  CsrFrontIO csr2front;
  CsrStatusIO csr_status;
  ExeCsrIO exe2csr;
  PreIduIssueIO pre_idu_issue;
  FTQLookupIO ftq_lookup;

public:
  SimContext *ctx;

  PreIduQueue *pre_idu_queue;
  Idu *idu;
  Ren *rename;
  Dispatch *dis;
  Isu *isu;
  Prf *prf;
  Exu *exu;
  Csr *csr;
  Rob *rob;
  AbstractLsu *lsu;

  Back_in in;
  Back_out out;
  MemReqIO *lsu_dcache_req_io;
  MemReqIO *lsu_dcache_wreq_io;
  MemRespIO *lsu_dcache_resp_io;
  MemReadyIO *lsu_dcache_wready_io;
  void init();
  void comb_csr_status();
  void comb();
  void seq();

  BackTop(SimContext *ctx) {
    this->ctx = ctx;
    pre_idu_queue = nullptr;
    lsu_dcache_req_io = &lsu2dcache_req;
    lsu_dcache_wreq_io = &lsu2dcache_wreq;
    lsu_dcache_resp_io = &dcache2lsu_resp;
    lsu_dcache_wready_io = &dcache2lsu_wready;
  };
  ~BackTop() {
    delete pre_idu_queue;
    delete rename;
    delete dis;
    delete idu;
    delete isu;
    delete exu;
    delete prf;
    delete rob;
    delete csr;
  }

  uint32_t number_PC = 0;

  void load_image(const std::string &filename);
  void restore_checkpoint(const std::string &filename);
  void restore_from_ref();

  uint32_t get_reg(uint8_t arch_idx) {
    return prf->reg_file[rename->arch_RAT_1[arch_idx]];
  }
};
