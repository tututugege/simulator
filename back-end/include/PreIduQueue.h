#pragma once

#include "FTQ.h"
#include "IO.h"
#include "InstructionBuffer.h"
#include "config.h"

class SimContext;

struct PreIduQueueIn {
  FrontPreIO *front2pre = nullptr;
  IduConsumeIO *idu_consume = nullptr;
  RobBroadcastIO *rob_bcast = nullptr;
  RobCommitIO *rob_commit = nullptr;
  ExuIdIO *idu_br_latch = nullptr;
  FtqPrfPcReqIO *ftq_prf_pc_req = nullptr;
  FtqRobPcReqIO *ftq_rob_pc_req = nullptr;
};

struct PreIduQueueOut {
  PreFrontIO *pre2front = nullptr;
  PreIssueIO *issue = nullptr;
  FtqPrfPcRespIO *ftq_prf_pc_resp = nullptr;
  FtqRobPcRespIO *ftq_rob_pc_resp = nullptr;
};

class PreIduQueue {
public:
  explicit PreIduQueue(SimContext *ctx = nullptr) : ctx(ctx) {}
  PreIduQueueIn in;
  PreIduQueueOut out;

  void init();
  void comb_begin();
  void comb_accept_front();
  void comb_fire();
  void comb_ftq_lookup_prf();
  void comb_ftq_lookup_rob();
  void seq();
  const FTQEntry *lookup_ftq_entry(uint32_t idx) const;
  bool ftq_train_meta_cursor_begin(uint32_t &cursor_idx) const;
  const FTQTrainMetaEntry *ftq_train_meta_cursor_peek(uint32_t cursor_idx) const;
  bool ftq_train_meta_cursor_advance(uint32_t &cursor_idx) const;

private:
  SimContext *ctx = nullptr;

  int ftq_alloc();
  void ftq_pop(int pop_cnt);
  void ftq_recover(int new_tail);
  void ftq_flush();

  InstructionBuffer ibuf;
  InstructionBuffer ibuf_1;

  FTQEntry ftq_lookup_entries[FTQ_SIZE];
  FTQEntry ftq_lookup_entries_1[FTQ_SIZE];
  FTQTrainMetaEntry ftq_train_meta_fifo[FTQ_SIZE];
  FTQTrainMetaEntry ftq_train_meta_fifo_1[FTQ_SIZE];
  wire<1> ftq_valid[FTQ_SIZE] = {};
  wire<1> ftq_valid_1[FTQ_SIZE] = {};
  reg<FTQ_IDX_WIDTH> ftq_head = 0;
  reg<FTQ_IDX_WIDTH> ftq_tail = 0;
  reg<bit_width_for_count(FTQ_SIZE + 1)> ftq_count = 0;
  wire<FTQ_IDX_WIDTH> ftq_head_1 = 0;
  wire<FTQ_IDX_WIDTH> ftq_tail_1 = 0;
  wire<bit_width_for_count(FTQ_SIZE + 1)> ftq_count_1 = 0;
};
