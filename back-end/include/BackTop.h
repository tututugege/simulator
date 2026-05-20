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
#include "RealLsu.h"
#include "Rob.h"
#include "TlbMmu.h"
#include "config.h"
#include <cstdint>
#include <memory>

class SimContext;
class PtwMemPort;
class PtwWalkPort;

struct Back_in : FrontPreIO {
  // from MemSubsystem
  PeripheralRespIO peripheral_resp;
  DcacheLsuIO dcache2lsu;

  Back_in() : FrontPreIO(), peripheral_resp{}, dcache2lsu{} {}
};

struct Back_out {
  // to front-end
  bool mispred;
  bool stall;
  bool flush;
  bool fence_i;
  bool itlb_flush;
  wire<1> *fire;
  uint32_t redirect_pc;
  InstEntry commit_entry[COMMIT_WIDTH];

  wire<32> sstatus;
  wire<32> mstatus;
  wire<32> satp;
  wire<2> privilege;

  // to MemSubsystem
  PeripheralReqIO peripheral_req;
  LsuDcacheIO lsu2dcache;

  Back_out()
      : mispred(false), stall(false), flush(false), fence_i(false),
        itlb_flush(false), fire(nullptr), redirect_pc(0), commit_entry{},
        sstatus{}, mstatus{}, satp{}, privilege{}, peripheral_req{},
        lsu2dcache{} {}
};

class Dispatch;
class Idu;
class Ren;
class Isu;
class Prf;
class Exu;
class Rob;
class Csr;
class AbstractMMU;
class MemSubsystem;

class BackTop {
private:
  FrontPreIO front2pre;
  PreFrontIO pre2front;

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
  FtqPrfPcReqIO ftq_prf_pc_req;
  FtqPrfPcRespIO ftq_prf_pc_resp;

  ExePrfIO exe2prf;
  ExeIssIO exe2iss;
  ExeLsuIO exe2lsu;

  LsuExeIO lsu2exe;
  LsuDisIO lsu2dis;
  LsuRobIO lsu2rob;
  LsuMMUIO lsu2mmu_io;         // LSU → DTLB request bus
  MMULsuIO mmu2lsu_io;         // DTLB → LSU response bus
  std::unique_ptr<TlbMmu> dtlb_mmu;
  void comb_lsu_mmu();

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
  PreIssueIO pre_issue;
  IduConsumeIO idu_consume;

public:
  SimContext *ctx;

  PreIduQueue *pre;
  Idu *idu;
  Ren *rename;
  Dispatch *dis;
  Isu *isu;
  Prf *prf;
  Exu *exu;
  Csr *csr;
  Rob *rob;
  RealLsu *lsu;

  Back_in in;
  Back_out out;
  FtqCommitInfoIO ftq_commit_info;
  CsrInterruptInjectIO csr_interrupt_inject_io;
  void init();
  void comb_csr_status();
  void comb();
  void seq();
  void set_lsu_ptw_mem_port(PtwMemPort *port);
  void set_lsu_ptw_walk_port(PtwWalkPort *port);

  BackTop(SimContext *ctx) {
    this->ctx = ctx;
    pre = nullptr;
    out.fire = nullptr;
  };
  ~BackTop() {
    delete pre;
    delete rename;
    delete dis;
    delete idu;
    delete isu;
    delete exu;
    delete prf;
    delete rob;
    delete csr;
    delete lsu;
  }

  uint32_t number_PC = 0;
  uint64_t ckpt_interval_inst_count = 0;

  void load_image(const std::string &filename);
  void restore_checkpoint(const std::string &filename);
  void restore_from_ref();

  uint32_t get_reg(uint8_t arch_idx) {
    return prf->reg_file[rename->arch_RAT_1[arch_idx]];
  }
};
