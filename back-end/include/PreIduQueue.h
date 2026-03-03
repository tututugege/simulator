#pragma once

#include "FTQ.h"
#include "IO.h"
#include "InstructionBuffer.h"
#include "config.h"

class SimContext;

struct PreIduIssueIO {
  InstructionBufferEntry entries[DECODE_WIDTH];
  PreIduIssueIO() {
    for (auto &e : entries) {
      e = {};
    }
  }
};

class PreIduQueueIn {
public:
  FrontDecIO *front2dec = nullptr;
  RenDecIO *ren2dec = nullptr;
  DecRenIO *idu_dec2ren = nullptr;
  RobBroadcastIO *rob_bcast = nullptr;
  RobCommitIO *rob_commit = nullptr;
  ExuIdIO *exu2id = nullptr;
};

class PreIduQueueOut {
public:
  DecFrontIO *dec2front = nullptr;
  PreIduIssueIO *issue = nullptr;
  FTQLookupIO *ftq_lookup = nullptr;
};

class PreIduQueue {
public:
  explicit PreIduQueue(SimContext *ctx = nullptr) : ctx(ctx) {}
  PreIduQueueIn in;
  PreIduQueueOut out;

  void init();
  void comb_begin();
  void comb_accept_front();
  void comb_consume_issue();
  void comb_flush_recover();
  void comb_commit_reclaim();
  void seq();

private:
  SimContext *ctx = nullptr;

  int ftq_alloc(const FTQEntry &entry);
  void ftq_pop(int pop_cnt);
  void ftq_recover(int new_tail);
  void ftq_flush();

  InstructionBuffer ibuf;

  FTQEntry ftq_entries[FTQ_SIZE];
  int ftq_head = 0;
  int ftq_tail = 0;
  int ftq_count = 0;
  int ftq_head_1 = 0;
  int ftq_tail_1 = 0;
  int ftq_count_1 = 0;

  bool ftq_flush_req = false;
  bool ftq_recover_req = false;
  int ftq_recover_tail = 0;
  bool ftq_alloc_req_valid = false;
  FTQEntry ftq_alloc_req_entry;
  bool ftq_alloc_success = false;
  int ftq_alloc_idx = -1;

  ExuIdIO br_latch;

  bool front_accept = false;
  InstructionBufferEntry push_entries[FETCH_WIDTH];
  int push_count = 0;
  int pop_count = 0;
};
