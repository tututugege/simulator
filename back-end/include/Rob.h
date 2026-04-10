#pragma once
#include "IO.h"
#include "config.h"

struct RobOut {
  RobDisIO *rob2dis;
  RobCsrIO *rob2csr;
  RobCommitIO *rob_commit;
  RobBroadcastIO *rob_bcast;
  FtqRobPcReqIO *ftq_pc_req;
};

struct RobIn {
  DisRobIO *dis2rob;
  CsrRobIO *csr2rob;
  LsuRobIO *lsu2rob;
  DecBroadcastIO *dec_bcast;
  ExuRobIO *exu2rob;
  FtqRobPcRespIO *ftq_pc_resp;
};

struct RobStoredInst {
  wire<32> diag_val;
  wire<AREG_IDX_WIDTH> dest_areg;
  wire<AREG_IDX_WIDTH> src1_areg;
  wire<PRF_IDX_WIDTH> dest_preg;
  wire<PRF_IDX_WIDTH> old_dest_preg;
  wire<1> dest_en;

  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> ftq_is_last;

  wire<1> mispred;
  wire<1> br_taken;

  wire<1> is_atomic;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<1> rob_flag;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<1> stq_flag;

  wire<ROB_CPLT_MASK_WIDTH> expect_mask;
  wire<ROB_CPLT_MASK_WIDTH> cplt_mask;

  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;
  wire<1> flush_pipe;

  wire<INST_TYPE_WIDTH> type;
  TmaMeta tma;
  DebugMeta dbg;

  RobStoredInst() { std::memset(this, 0, sizeof(RobStoredInst)); }

  static RobStoredInst from_dis_rob_inst(const DisRobIO::DisRobInst &src) {
    RobStoredInst dst;
    dst.diag_val = src.diag_val;
    dst.dest_areg = src.dest_areg;
    dst.src1_areg = src.src1_areg;
    dst.dest_preg = src.dest_preg;
    dst.old_dest_preg = src.old_dest_preg;
    dst.ftq_idx = src.ftq_idx;
    dst.ftq_offset = src.ftq_offset;
    dst.ftq_is_last = src.ftq_is_last;
    dst.mispred = src.mispred;
    dst.br_taken = src.br_taken;
    dst.dest_en = src.dest_en;
    dst.is_atomic = src.is_atomic;
    dst.func3 = src.func3;
    dst.func7 = src.func7;
    dst.imm = src.imm;
    dst.rob_idx = src.rob_idx;
    dst.rob_flag = src.rob_flag;
    dst.stq_idx = src.stq_idx;
    dst.stq_flag = src.stq_flag;
    dst.expect_mask = src.expect_mask;
    dst.cplt_mask = src.cplt_mask;
    dst.page_fault_inst = src.page_fault_inst;
    dst.page_fault_load = false;
    dst.page_fault_store = false;
    dst.illegal_inst = src.illegal_inst;
    dst.type = src.type;
    dst.tma = src.tma;
    dst.dbg = src.dbg;
    dst.flush_pipe = src.flush_pipe;
    return dst;
  }

  RobCommitIO::RobCommitInst to_commit_inst() const {
    RobCommitIO::RobCommitInst dst;
    dst.diag_val = diag_val;
    dst.dest_areg = dest_areg;
    dst.dest_preg = dest_preg;
    dst.old_dest_preg = old_dest_preg;
    dst.ftq_idx = ftq_idx;
    dst.ftq_offset = ftq_offset;
    dst.ftq_is_last = ftq_is_last;
    dst.mispred = mispred;
    dst.br_taken = br_taken;
    dst.dest_en = dest_en;
    dst.func7 = func7;
    dst.rob_idx = rob_idx;
    dst.rob_flag = rob_flag;
    dst.stq_idx = stq_idx;
    dst.stq_flag = stq_flag;
    dst.page_fault_inst = page_fault_inst;
    dst.page_fault_load = page_fault_load;
    dst.page_fault_store = page_fault_store;
    dst.illegal_inst = illegal_inst;
    dst.type = type;
    dst.tma = tma;
    dst.dbg = dbg;
    dst.flush_pipe = flush_pipe;
    return dst;
  }
};

struct RobStoredEntry {
  wire<1> valid;
  RobStoredInst uop;
};

class Rob {
public:
  Rob(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  void init();
  void seq();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）
  void comb_ready();
  void comb_ftq_pc_req();
  void comb_commit();
  void comb_complete();
  void comb_fire();
  void comb_branch();
  void comb_flush();

  RobIn in;
  RobOut out;

  // 状态
  RobStoredEntry entry[ROB_BANK_NUM][ROB_LINE_NUM];
  reg<clog2(ROB_LINE_NUM)> enq_ptr;
  reg<clog2(ROB_LINE_NUM)> deq_ptr;
  reg<1> enq_flag;
  reg<1> deq_flag;

  RobStoredEntry entry_1[ROB_BANK_NUM][ROB_LINE_NUM];
  wire<clog2(ROB_LINE_NUM)> enq_ptr_1;
  wire<clog2(ROB_LINE_NUM)> deq_ptr_1;
  wire<1> enq_flag_1;
  wire<1> deq_flag_1;

private:
  int stall_cycle = 0;
  bool is_empty() { return (enq_ptr == deq_ptr) && (enq_flag == deq_flag); };
  bool is_full() { return (enq_ptr == deq_ptr) && (enq_flag != deq_flag); };
};
