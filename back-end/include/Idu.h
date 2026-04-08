#pragma once
#include "IO.h"
#include "PreIduQueue.h"
#include "config.h"
class SimContext;

struct IduIn {
  PreIduIssueIO *issue;
  RenDecIO *ren2dec;
  RobBroadcastIO *rob_bcast;
  ExuIdIO *exu2id; // [New] From Exu
};

struct IduOut {
  DecRenIO *dec2ren;
  DecBroadcastIO *dec_bcast;
  IduConsumeIO *idu_consume;
};

class Idu {
public:
  Idu(SimContext *ctx, int max_br = 1) {
    this->ctx = ctx;
    this->max_br_per_cycle = max_br;
  }
  SimContext *ctx;
  int max_br_per_cycle;
  IduIn in;
  IduOut out;
  void decode(DecRenIO::DecRenInst &uop, uint32_t inst);

  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）
  void comb_decode(); // 译码并分配tag
  void comb_branch(); // 分支处理
  void comb_fire();  // 发射握手与分支tag推进
  void seq();              // 时钟跳变，状态更新

  // 状态
  reg<BR_MASK_WIDTH> now_br_mask;
  reg<BR_MASK_WIDTH> br_mask_cp[MAX_BR_NUM];
  reg<BR_MASK_WIDTH> pending_free_mask; // 延迟一拍释放，避免同拍复用 br_id
  reg<1> tag_vec[MAX_BR_NUM];
  ExuIdIO br_latch;

  // 下一周期状态
  wire<BR_MASK_WIDTH> now_br_mask_1;
  wire<BR_MASK_WIDTH> br_mask_cp_1[MAX_BR_NUM];
  wire<BR_MASK_WIDTH> pending_free_mask_1;
  wire<1> tag_vec_1[MAX_BR_NUM];
  ExuIdIO br_latch_1;
};
