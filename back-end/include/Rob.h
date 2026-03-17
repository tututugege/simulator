#pragma once
#include "IO.h"
#include "config.h"

class RobOut {
public:
  RobDisIO *rob2dis;
  RobCsrIO *rob2csr;
  RobCommitIO *rob_commit;
  RobBroadcastIO *rob_bcast;
  FtqRobPcReqIO *ftq_pc_req;
};

class RobIn {
public:
  DisRobIO *dis2rob;
  CsrRobIO *csr2rob;
  LsuRobIO *lsu2rob;
  DecBroadcastIO *dec_bcast;
  ExuRobIO *exu2rob;
  FtqRobPcRespIO *ftq_pc_resp;
};

class Rob {
public:
  Rob(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  void init();
  void seq();
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
  InstEntry entry[ROB_BANK_NUM][ROB_LINE_NUM];
  reg<clog2(ROB_LINE_NUM)> enq_ptr;
  reg<clog2(ROB_LINE_NUM)> deq_ptr;
  reg<1> enq_flag;
  reg<1> deq_flag;

  InstEntry entry_1[ROB_BANK_NUM][ROB_LINE_NUM];
  wire<clog2(ROB_LINE_NUM)> enq_ptr_1;
  wire<clog2(ROB_LINE_NUM)> deq_ptr_1;
  wire<1> enq_flag_1;
  wire<1> deq_flag_1;

private:
  int stall_cycle = 0;
  bool is_empty() { return (enq_ptr == deq_ptr) && (enq_flag == deq_flag); };
  bool is_full() { return (enq_ptr == deq_ptr) && (enq_flag != deq_flag); };
};
