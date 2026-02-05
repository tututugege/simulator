#pragma once
#include "IO.h"
#include "config.h"
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
  Idu(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
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
  reg<4> tag_list[MAX_BR_NUM];
  reg<4> enq_ptr;
  reg<1> tag_vec[MAX_BR_NUM];
  reg<4> now_tag;

  // 下一周期状态
  wire<4> tag_list_1[MAX_BR_NUM];
  wire<4> enq_ptr_1;
  wire<1> tag_vec_1[MAX_BR_NUM];
  wire<4> now_tag_1;
};
