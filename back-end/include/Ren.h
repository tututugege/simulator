#pragma once
#include "IO.h"
#include "config.h"

class BackTop;

struct RenIn {
  DecRenIO *dec2ren;
  DecBroadcastIO *dec_bcast;
  DisRenIO *dis2ren;
  RobBroadcastIO *rob_bcast;
  RobCommitIO *rob_commit;
};

struct RenOut {
  RenDecIO *ren2dec;
  RenDisIO *ren2dis;
};

class Ren {
public:
  Ren(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  RenIn in;
  RenOut out;

  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）
  void comb_select();
  void comb_rename(); // 重命名
  void comb_fire();
  void comb_alloc(); // 分配寄存器
  void comb_pipeline();
  void seq();

  // register
  DecRenIO::DecRenInst inst_r[DECODE_WIDTH];
  reg<1> inst_valid[DECODE_WIDTH];
  reg<PRF_IDX_WIDTH> arch_RAT[ARF_NUM + 1];
  reg<PRF_IDX_WIDTH> spec_RAT[ARF_NUM + 1];
  reg<PRF_IDX_WIDTH> RAT_checkpoint[MAX_BR_NUM][ARF_NUM + 1];
  reg<PRF_IDX_WIDTH> free_list[PRF_NUM];
  reg<PRF_IDX_WIDTH> free_head; // speculative head
  reg<PRF_IDX_WIDTH> free_head_commit;
  reg<PRF_IDX_WIDTH> free_tail;
  reg<PRF_IDX_WIDTH> alloc_checkpoint_head[MAX_BR_NUM];

  DecRenIO::DecRenInst inst_r_1[DECODE_WIDTH];
  wire<1> inst_valid_1[DECODE_WIDTH];
  wire<PRF_IDX_WIDTH> arch_RAT_1[ARF_NUM + 1];
  wire<PRF_IDX_WIDTH> spec_RAT_1[ARF_NUM + 1];
  wire<PRF_IDX_WIDTH> RAT_checkpoint_1[MAX_BR_NUM][ARF_NUM + 1];
  wire<PRF_IDX_WIDTH> free_list_1[PRF_NUM];
  wire<PRF_IDX_WIDTH> free_head_1;
  wire<PRF_IDX_WIDTH> free_head_commit_1;
  wire<PRF_IDX_WIDTH> free_tail_1;
  wire<PRF_IDX_WIDTH> alloc_checkpoint_head_1[MAX_BR_NUM];
};
