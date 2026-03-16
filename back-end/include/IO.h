#pragma once

#include "ModuleIOs.h"
#include "config.h"
#include "util.h"
#include <bitset>
#include <cstdint>
#include <sys/types.h>

struct DecRenIO {

  InstInfo uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];
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
  wire<32> tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
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
    for (auto &tage_tag_0 : tage_tag)
      for (auto &tag : tage_tag_0)
        tag = {};

    for (auto &v : page_fault_inst)
      v = {};
  }
};

struct DecBroadcastIO {

  wire<1> mispred;
  wire<BR_MASK_WIDTH> br_mask;
  wire<BR_TAG_WIDTH> br_id;
  wire<ROB_IDX_WIDTH> redirect_rob_idx;
  mask_t clear_mask; // Bits to clear from all in-flight br_masks (resolved branches)

  DecBroadcastIO() {
    mispred = {};
    br_mask = {};
    br_id = {};
    redirect_rob_idx = {};
    clear_mask = 0;
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

  InstInfo uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];
  wire<1> dis_fire[DECODE_WIDTH];

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

  InstInfo uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];

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

  wire<ROB_IDX_WIDTH> head_rob_idx;
  wire<1> head_valid;
  wire<ROB_IDX_WIDTH> head_incomplete_rob_idx;
  wire<1> head_incomplete_valid;

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
    
    head_rob_idx = {};
    head_valid = {};
    head_incomplete_rob_idx = {};
    head_incomplete_valid = {};
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
  wire<BR_TAG_WIDTH> br_id;
  int ftq_idx; // FTQ index of mispredicting branch, for tail recovery
  mask_t clear_mask; // OR of all resolved branches' (1 << br_id) this cycle

  ExuIdIO() {
    mispred = {};
    redirect_pc = {};
    redirect_rob_idx = {};
    br_id = {};
    ftq_idx = 0;
    clear_mask = 0;
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

struct PtwWalkReq {
  wire<32> vaddr;
  wire<32> satp;
  wire<32> access_type; // 0=Fetch, 1=Load, 2=Store

  PtwWalkReq() {
    vaddr = {};
    satp = {};
    access_type = {};
  }
};

struct PtwWalkResp {
  wire<1> fault;
  wire<32> leaf_pte;
  wire<8> leaf_level; // 1: L1 leaf, 0: L0 leaf

  PtwWalkResp() {
    fault = {};
    leaf_pte = {};
    leaf_level = {};
  }
};

struct PtwMemReqIO {
  wire<1> en;
  wire<32> paddr;

  PtwMemReqIO() {
    en = {};
    paddr = {};
  }
};

struct PtwMemRespIO {
  wire<1> valid;
  wire<32> data;

  PtwMemRespIO() {
    valid = {};
    data = {};
  }
};

struct PtwWalkReqIO {
  wire<1> en;
  PtwWalkReq req;

  PtwWalkReqIO() {
    en = {};
    req = {};
  }
};

struct PtwWalkRespIO {
  wire<1> valid;
  PtwWalkResp resp;

  PtwWalkRespIO() {
    valid = {};
    resp = {};
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

struct PeripheralInIO{
  wire<1> is_mmio;
  wire<1> wen;
  wire<32> mmio_addr;
  wire<32> mmio_wdata;
  wire<8> mmio_wstrb;
  MicroOp uop;

  PeripheralInIO() {
    is_mmio = {};
    mmio_addr = {};
    mmio_wdata = {};
    mmio_wstrb = {};
    uop = {};
  }
};
struct PeripheralOutIO{
  wire<1> is_mmio;
  wire<1> ready;
  wire<32> mmio_rdata;
  MicroOp uop;

  PeripheralOutIO() {
    is_mmio = {};
    ready = {};
    mmio_rdata = {};
    uop = {};
  }

};

struct PeripheralIO {
  PeripheralInIO in;
  PeripheralOutIO out;
};

// STQ 条目结构（定义在此以供 StoreReq 使用）
struct StqEntry {
  bool valid        = false;
  bool addr_valid   = false;
  bool data_valid   = false;
  bool committed    = false;
  bool done         = false;
  bool is_mmio      = false;
  bool send         = false; // Whether the store has been sent to DCache (for replay logic)
  uint8_t replay = 0;
  uint32_t addr     = 0;
  uint32_t p_addr   = 0;
  uint32_t suppress_write = 0; // For MMIO: bits to suppress in the write (e.g., for LR/SC)
  uint32_t data     = 0;
  uint32_t func3    = 0;
  mask_t   br_mask  = {};
  uint32_t rob_idx  = 0;
  uint32_t rob_flag = 0;
};

struct LoadReq {
    wire<1> valid;
    wire<32> addr;
    MicroOp uop;
    size_t req_id;

    LoadReq() : valid(false), addr(0), uop(), req_id(0) {}
};

struct StoreReq {
    wire<1> valid;
    wire<32> addr;
    wire<32> data;
    wire<8> strb;
    StqEntry uop;
    size_t req_id;

    StoreReq() : valid(false), addr(0), data(0), strb(0xF), uop(), req_id(0) {}
};

// Load响应结构
struct LoadResp {
    wire<1> valid;
    wire<32> data;
    MicroOp uop;
    size_t req_id;
    wire<2> replay;
    LoadResp() : valid(false), data(0), uop(), req_id(0), replay(0) {}
};

// Store响应结构
struct StoreResp {
    wire<1> valid;
    wire<2> replay;         
    size_t req_id;
    wire<1> is_cache_miss;

    StoreResp() : valid(false), replay(0), req_id(0), is_cache_miss(false) {}
};

// 请求端口集合（支持4个Load + 4个Store）
struct DCacheReqPorts {

    LoadReq load_ports[LSU_LDU_COUNT];
    StoreReq store_ports[LSU_STA_COUNT];

    void clear() {
        for (int i = 0; i < LSU_LDU_COUNT; i++) {
            load_ports[i].valid = false;
        }
        for (int i = 0; i < LSU_STA_COUNT; i++) {
            store_ports[i].valid = false;
        }
    }
};
struct ReplayResp{
    wire<2> replay;
    size_t replay_addr;
    wire<8> free_slots;

    ReplayResp() : replay(0), replay_addr(0), free_slots(0) {}
};

// 响应端口集合
struct DCacheRespPorts {
    LoadResp load_resps[LSU_LDU_COUNT];
    StoreResp store_resps[LSU_STA_COUNT];
    ReplayResp replay_resp;

    void clear() {
        for (int i = 0; i < LSU_LDU_COUNT; i++) {
            load_resps[i].valid = false;
        }
        for (int i = 0; i < LSU_STA_COUNT; i++) {
            store_resps[i].valid = false;
            store_resps[i].replay = 0;
            store_resps[i].is_cache_miss = false;
        }
        replay_resp = ReplayResp();
    }
};

struct LsuDcacheIO {
  DCacheReqPorts req_ports;

  LsuDcacheIO() {
    req_ports.clear();
  }
};

struct DcacheLsuIO
{
  DCacheRespPorts resp_ports;

  DcacheLsuIO() {
    resp_ports.clear();
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

  int stq_tail;                              // 当前分配指针
  bool stq_tail_flag;                        // stq_tail 对应 ring 代际位
  int stq_free;                              // 剩余空闲条目数
  int ldq_free;                              // 剩余 Load 队列空闲数
  int ldq_alloc_idx[MAX_LDQ_DISPATCH_WIDTH]; // 本拍可分配的 LDQ 索引序列

  LsuDisIO() {
    stq_tail = 0;
    stq_tail_flag = false;
    stq_free = 0;
    ldq_free = 0;
    for (auto &v : ldq_alloc_idx)
      v = -1;
  }
};

struct LsuRobIO {
  std::bitset<ROB_NUM> miss_mask;
  wire<1> committed_store_pending;

  LsuRobIO() {
    miss_mask.reset();
    committed_store_pending = 0;
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
  mask_t br_mask[MAX_STQ_DISPATCH_WIDTH];
  uint32_t func3[MAX_STQ_DISPATCH_WIDTH];
  uint32_t rob_idx[MAX_STQ_DISPATCH_WIDTH];
  uint32_t rob_flag[MAX_STQ_DISPATCH_WIDTH];

  bool ldq_alloc_req[MAX_LDQ_DISPATCH_WIDTH];
  uint32_t ldq_idx[MAX_LDQ_DISPATCH_WIDTH];
  mask_t ldq_br_mask[MAX_LDQ_DISPATCH_WIDTH];
  uint32_t ldq_rob_idx[MAX_LDQ_DISPATCH_WIDTH];
  uint32_t ldq_rob_flag[MAX_LDQ_DISPATCH_WIDTH];

  DisLsuIO() {
    for (auto &v : alloc_req)
      v = false;
    for (auto &v : br_mask)
      v = 0;
    for (auto &v : func3)
      v = 0;
    for (auto &v : rob_idx)
      v = 0;
    for (auto &v : rob_flag)
      v = 0;
    for (auto &v : ldq_alloc_req)
      v = false;
    for (auto &v : ldq_idx)
      v = 0;
    for (auto &v : ldq_br_mask)
      v = 0;
    for (auto &v : ldq_rob_idx)
      v = 0;
    for (auto &v : ldq_rob_flag)
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
