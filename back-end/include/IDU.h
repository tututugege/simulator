#pragma once
#include "IO.h"
#include "config.h"

class IDU_IO {
public:
  Front_Dec *front2dec;
  Dec_Front *dec2front;

  Dec_Ren *dec2ren;
  Ren_Dec *ren2dec;

  Dec_Broadcast *dec_bcast;

  Prf_Dec *prf2dec;
  Rob_Broadcast *rob_bcast;
  Rob_Commit *commit;
};

class IDU {
public:
  IDU_IO io;
  void init();
  void comb_decode();      // 译码并分配tag
  void comb_branch();      // 分支处理
  void comb_fire();        // 与前端握手
  void comb_flush();       // flush处理
  void comb_release_tag(); // 释放分支tag
  void seq();              // 时钟跳变，状态更新

  // 中间信号
  wire4_t alloc_tag; // 新tag

  // 状态
  reg4_t tag_list[MAX_BR_NUM];
  reg4_t enq_ptr;
  reg4_t deq_ptr;
  reg1_t tag_vec[MAX_BR_NUM];
  reg4_t now_tag;

  // 下一周期状态
  wire4_t tag_list_1[MAX_BR_NUM];
  wire4_t enq_ptr_1;
  wire4_t deq_ptr_1;
  wire1_t tag_vec_1[MAX_BR_NUM];
  wire4_t now_tag_1;
};
