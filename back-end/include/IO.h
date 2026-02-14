#pragma once

#include "config.h"
#include <cstdint>
#include <sys/types.h>
#include <util.h>
#include "ModuleIOs.h"

struct DecRenIO {

  InstInfo uop[FETCH_WIDTH];
  wire<1> valid[FETCH_WIDTH];
  DecRenIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
  }
};

struct RenDecIO {

  wire<1> ready;

  RenDecIO() { ready = {}; }
};

struct DecFrontIO {

  wire<1> fire[FETCH_WIDTH];
  wire<1> ready;

  DecFrontIO() {
    for (auto &v : fire)
      v = {};
    ready = {};
  }
};

struct FrontDecIO {

  wire<32> inst[FETCH_WIDTH];
  wire<32> pc[FETCH_WIDTH];
  wire<1> valid[FETCH_WIDTH];
  wire<1> predict_dir[FETCH_WIDTH];

  wire<1> alt_pred[FETCH_WIDTH];
  wire<8> altpcpn[FETCH_WIDTH];
  wire<8> pcpn[FETCH_WIDTH];
  wire<32> predict_next_fetch_address[FETCH_WIDTH];
  wire<32> tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  wire<1> page_fault_inst[FETCH_WIDTH];

  FrontDecIO() {
    for (auto &v : inst)
      v = {};
    for (auto &v : pc)
      v = {};
    for (auto &v : valid)
      v = {};
    for (auto &v : predict_dir)
      v = {};
    for (auto &v : alt_pred)
      v = {};
    for (auto &v : altpcpn)
      v = {};
    for (auto &v : pcpn)
      v = {};
    for (auto &v : predict_next_fetch_address)
      v = {};
    for (auto &tage_idx_0 : tage_idx)
      for (auto &idx : tage_idx_0)
        idx = {};

    for (auto &v : page_fault_inst)
      v = {};
  }
};

struct DecBroadcastIO {

  wire<1> mispred;
  wire<BR_MASK_WIDTH> br_mask;
  wire<BR_TAG_WIDTH> br_tag;
  wire<ROB_IDX_WIDTH> redirect_rob_idx;

  DecBroadcastIO() {
    mispred = {};
    br_mask = {};
    br_tag = {};
    redirect_rob_idx = {};
  }
};

struct RobCommitIO {

  InstEntry commit_entry[COMMIT_WIDTH];

  RobCommitIO() {
    for (auto &v : commit_entry)
      v = {};
  }
};

struct RobDisIO {

  wire<1> ready;
  wire<1> empty;
  wire<1> stall;
  wire<ROB_IDX_WIDTH> enq_idx;
  wire<1> rob_flag;
  wire<1> head_is_memory;
  wire<1> head_is_miss;
  wire<1> head_not_ready;

  RobDisIO() {
    ready = {};
    empty = {};
    stall = {};
    enq_idx = {};
    rob_flag = {};
    head_is_memory = {};
    head_not_ready = {};
  }
};

struct DisRobIO {

  InstInfo uop[FETCH_WIDTH];
  wire<1> valid[FETCH_WIDTH];
  wire<1> dis_fire[FETCH_WIDTH];

  DisRobIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
    for (auto &v : dis_fire)
      v = {};
  }
};

struct RenDisIO {

  InstInfo uop[FETCH_WIDTH];
  wire<1> valid[FETCH_WIDTH];

  RenDisIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
  }
};

struct DisRenIO {

  wire<1> ready;

  DisRenIO() { ready = {}; }
};

struct PrfAwakeIO {

  WakeInfo wake[LSU_LOAD_WB_WIDTH];

  PrfAwakeIO() {
    for (auto &w : wake)
      w = {};
  }
};

struct DisIssIO {

  UopEntry req[IQ_NUM][MAX_IQ_DISPATCH_WIDTH];
  DisIssIO() {
    for (auto &iq_req : req)
      for (auto &r : iq_req)
        r = {};
  }
};

struct IssDisIO {

  int ready_num[IQ_NUM];

  IssDisIO() {
    for (auto &v : ready_num)
      v = 0;
  }
};

struct IssAwakeIO {

  WakeInfo wake[MAX_WAKEUP_PORTS];

  IssAwakeIO() {
    for (auto &v : wake)
      v = {};
  }
};

struct RobBroadcastIO {

  wire<1> flush;
  wire<1> mret;
  wire<1> sret;
  wire<1> ecall;
  wire<1> exception;
  wire<1> fence;
  wire<1> fence_i;

  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;
  wire<1> interrupt;
  wire<32> trap_val;
  wire<32> pc;

  RobBroadcastIO() {
    flush = {};
    mret = {};
    sret = {};
    ecall = {};
    exception = {};
    fence = {};
    fence_i = {};
    page_fault_inst = {};
    page_fault_load = {};
    page_fault_store = {};
    illegal_inst = {};
    interrupt = {};
    trap_val = {};
    pc = {};
  }
};

struct IssPrfIO {

  UopEntry iss_entry[ISSUE_WIDTH];

  IssPrfIO() {
    for (auto &v : iss_entry)
      v = {};
  }
};

struct PrfExeIO {

  UopEntry iss_entry[ISSUE_WIDTH];

  PrfExeIO() {
    for (auto &v : iss_entry)
      v = {};
  }
};

struct ExePrfIO {

  UopEntry entry[ISSUE_WIDTH];
  UopEntry bypass[TOTAL_FU_COUNT];

  ExePrfIO() {
    for (auto &v : entry)
      v = {};

    for (auto &v : bypass)
      v = {};
  }
};

struct ExeIssIO {

  wire<1> ready[ISSUE_WIDTH];
  int64_t fu_ready_mask[ISSUE_WIDTH];

  ExeIssIO() {
    for (auto &v : ready)
      v = {};
    for (auto &v : fu_ready_mask)
      v = {};
  }
};


struct ExuRobIO {
  UopEntry entry[ISSUE_WIDTH];

  ExuRobIO() {
    for (auto &v : entry)
      v = {};
  }
};


struct ExuIdIO {

  wire<1> mispred;
  wire<32> redirect_pc;
  wire<ROB_IDX_WIDTH> redirect_rob_idx;
  wire<BR_TAG_WIDTH> br_tag;
  int ftq_idx;  // FTQ index of mispredicting branch, for tail recovery

  ExuIdIO() {
    mispred = {};
    redirect_pc = {};
    redirect_rob_idx = {};
    br_tag = {};
    ftq_idx = 0;
  }
};

struct ExeCsrIO {

  wire<1> we;
  wire<1> re;
  wire<12> idx;
  wire<32> wdata;
  wire<32> wcmd;

  ExeCsrIO() {
    we = {};
    re = {};
    idx = {};
    wdata = {};
    wcmd = {};
  }
};

struct CsrExeIO {

  wire<32> rdata;

  CsrExeIO() { rdata = {}; }
};

struct CsrRobIO {

  wire<1> interrupt_req;

  CsrRobIO() { interrupt_req = {}; }
};

struct CsrFrontIO {

  wire<32> epc;
  wire<32> trap_pc;

  CsrFrontIO() {
    epc = {};
    trap_pc = {};
  }
};

struct CsrStatusIO {

  wire<32> sstatus;
  wire<32> mstatus;
  wire<32> satp;
  wire<2> privilege;

  CsrStatusIO() {
    sstatus = {};
    mstatus = {};
    satp = {};
    privilege = {};
  }
};

struct RobCsrIO {

  wire<1> interrupt_resp;
  wire<1> commit;

  RobCsrIO() {
    interrupt_resp = {};
    commit = {};
  }
};

struct MemReqIO {

  wire<1> en;
  wire<1> wen;
  wire<32> addr;
  wire<32> wdata;
  wire<8> wstrb;

  MicroOp uop;

  MemReqIO() {
    en = {};
    wen = {};
    addr = {};
    wdata = {};
    wstrb = {};
    uop = {};
  }
};

struct MemReadyIO {

  wire<1> ready;

  MemReadyIO() { ready = {}; }
};

struct MemRespIO {

  wire<1> wen;
  wire<1> valid;
  wire<32> data;

  wire<32> addr;
  MicroOp uop;

  MemRespIO() {
    wen = {};
    valid = {};
    data = {};
    addr = {};
    uop = {};
  }
};

struct DcacheControlIO {

  wire<1> flush;
  wire<1> mispred;
  wire<BR_MASK_WIDTH> br_mask;

  DcacheControlIO() {
    flush = {};
    mispred = {};
    br_mask = {};
  }
};

struct DcacheMshrIO {

  wire<1> valid;
  wire<1> wen;
  wire<32> addr;
  wire<32> wstrb;
  wire<32> wdata;

  MicroOp uop;

  DcacheMshrIO() {
    valid = {};
    wen = {};
    addr = {};
    wstrb = {};
    wdata = {};
    uop = {};
  }
};

struct WbArbiterDcacheIO {

  wire<1> stall_ld;
  wire<1> stall_st;

  WbArbiterDcacheIO() {
    stall_ld = {};
    stall_st = {};
  }
};

struct DcacheWritebufferIO {

  wire<1> valid;
  wire<32> wdata;

  wire<1> flush;
  wire<1> mispred;
  wire<BR_MASK_WIDTH> br_mask;

  MicroOp uop;

  DcacheWritebufferIO() {
    valid = {};
    wdata = {};
    flush = {};
    mispred = {};
    br_mask = {};
    uop = {};
  }
};

struct WritebufferDcacheIO {

  wire<1> stall;

  WritebufferDcacheIO() { stall = {}; }
};

struct ExmemControlIO {

  wire<1> en;
  wire<1> wen;
  wire<8> sel;
  wire<8> len;
  wire<1> done;
  wire<1> last;
  wire<2> size;
  wire<32> addr;
  wire<32> wdata;

  ExmemControlIO() {
    en = {};
    wen = {};
    sel = {};
    len = {};
    done = {};
    last = {};
    size = {};
    addr = {};
    wdata = {};
  }
};
struct ExmemDataIO {

  wire<32> data;
  wire<1> last;
  wire<1> done;

  ExmemDataIO() {
    data = {};
    last = {};
    done = {};
  }
};
struct ExmemIO {

  ExmemControlIO control;
  ExmemDataIO data;

  ExmemIO() {
    control = {};
    data = {};
  }
};

struct MshrWbIO {

  wire<1> valid;
  wire<32> addr;
  wire<32> data[4];

  MshrWbIO() {
    valid = {};
    addr = {};
    for (auto &v : data)
      v = {};
  }
};

struct WbMshrIO {

  wire<1> ready;

  WbMshrIO() { ready = {}; }
};
struct WbArbiterIO {

  wire<1> arbiter_priority;

  WbArbiterIO() { arbiter_priority = {}; }
};

struct MshrFwdIO {

  wire<1> valid;
  wire<32> addr;
  wire<32> rdata;

  MshrFwdIO() {
    valid = {};
    addr = {};
    rdata = {};
  }
};

struct MshrArbiterIO {

  bool prority;

  MshrArbiterIO() { prority = false; }
};

struct PrfDcacheIO {

  bool stall;

  PrfDcacheIO() { stall = false; }
};

struct LsuDisIO {

  int stq_tail; // 当前分配指针
  int stq_free; // 剩余空闲条目数
  int ldq_free; // 剩余 Load 队列空闲数

  LsuDisIO() {
    stq_tail = 0;
    stq_free = 0;
    ldq_free = 0;
  }
};

struct LsuRobIO {
  wire<ROB_NUM> miss_mask;

  LsuRobIO() {
    miss_mask = 0;
  }
};

struct LsuExeIO {

  UopEntry wb_req[LSU_LOAD_WB_WIDTH];
  UopEntry sta_wb_req[LSU_STA_COUNT];

  LsuExeIO() {
    for (auto &v : wb_req)
      v = {};
    for (auto &v : sta_wb_req)
      v = {};
  }
};

struct DisLsuIO {

  bool alloc_req[MAX_STQ_DISPATCH_WIDTH];
  uint32_t tag[MAX_STQ_DISPATCH_WIDTH]; // TODO: use wire<BR_TAG_WIDTH>?
  uint32_t func3[MAX_STQ_DISPATCH_WIDTH];
  uint32_t rob_idx[MAX_STQ_DISPATCH_WIDTH];
  uint32_t rob_flag[MAX_STQ_DISPATCH_WIDTH];

  DisLsuIO() {
    for (auto &v : alloc_req)
      v = false;
    for (auto &v : tag)
      v = 0;
  }
};

struct ExeLsuIO {

  UopEntry agu_req[LSU_AGU_COUNT];
  UopEntry sdu_req[LSU_SDU_COUNT];

  ExeLsuIO() {
    for (auto &v : agu_req)
      v = {};
    for (auto &v : sdu_req)
      v = {};
  }
};
