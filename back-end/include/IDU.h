#pragma once
#include "IO.h"
#include "config.h"

class IDU_IN {
public:
  Front_Dec *front2dec;
  Ren_Dec *ren2dec;
  Prf_Dec *prf2dec;
  Rob_Broadcast *rob_bcast;
  Rob_Commit *commit;
};

class IDU_OUT {
public:
  Dec_Front *dec2front;
  Dec_Ren *dec2ren;
  Dec_Broadcast *dec_bcast;
};

class IDU {
public:
  IDU_IN in;
  IDU_OUT out;

  void init();
  void comb_decode();      // 译码并分配tag
  void comb_branch();      // 分支处理
  void comb_fire();        // 与前端握手
  void comb_flush();       // flush处理
  void comb_release_tag(); // 释放分支tag
  void seq();              // 时钟跳变，状态更新

  // 状态
  reg4_t tag_list[MAX_BR_NUM];
  reg4_t enq_ptr;
  reg1_t tag_vec[MAX_BR_NUM];
  reg4_t now_tag;

  // 下一周期状态
  wire4_t tag_list_1[MAX_BR_NUM];
  wire4_t enq_ptr_1;
  wire1_t tag_vec_1[MAX_BR_NUM];
  wire4_t now_tag_1;
};
