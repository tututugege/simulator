#pragma once
#include "IO.h"
#include "config.h"
#include "config.h"
#include "FTQ.h"
class SimContext;

class IduIn {
public:
  FrontDecIO *front2dec;
  RenDecIO *ren2dec;
  PrfDecIO *prf2dec;
  RobBroadcastIO *rob_bcast;
  RobCommitIO *commit;
};

class IduOut {
public:
  DecFrontIO *dec2front;
  DecRenIO *dec2ren;
  DecBroadcastIO *dec_bcast;
};

class Idu {
public:
  Idu(SimContext *ctx, FTQ *ftq, int max_br = 1) {
    this->ctx = ctx;
    this->ftq = ftq;
    this->max_br_per_cycle = max_br;
  }
  SimContext *ctx;
  FTQ *ftq;
  int max_br_per_cycle;
  IduIn in;
  IduOut out;
  void decode(InstUop &uop, uint32_t inst);

  void init();
  void comb_decode();      // 译码并分配tag
  void comb_branch();      // 分支处理
  void comb_fire();        // 与前端握手
  void comb_flush();       // flush处理
  void comb_release_tag(); // 释放分支tag
  void seq();              // 时钟跳变，状态更新

  IduIO get_hardware_io(); // 获取硬件级别 IO (Hardware Reference)

  // 状态
  reg<BR_TAG_WIDTH> tag_list[MAX_BR_NUM];
  reg<BR_TAG_WIDTH> enq_ptr;
  reg<BR_TAG_WIDTH> now_tag;
  reg<1> tag_vec[MAX_BR_NUM];

  // 下一周期状态
  wire<BR_TAG_WIDTH> tag_list_1[MAX_BR_NUM];
  wire<BR_TAG_WIDTH> enq_ptr_1;
  wire<BR_TAG_WIDTH> now_tag_1;
  wire<1> tag_vec_1[MAX_BR_NUM];
};
