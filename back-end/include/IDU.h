#pragma once
#include "IO.h"
#include "config.h"
#include <cstdint>

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
  void comb_decode(); // 译码并分配tag
  void comb_branch(); // 分支处理
  void comb_fire();   // 与前端握手
  void comb_flush();
  void comb_release_tag(); // 释放分支tag
  void seq();

  // 中间信号
  int pop = 0;
  bool push = false;  // 是否分配了新tag
  uint32_t alloc_tag; // 新tag

  // 状态
  uint8_t tag_list[MAX_BR_NUM];
  uint8_t enq_ptr;
  uint8_t deq_ptr;
  bool tag_vec[MAX_BR_NUM];
  int now_tag;

  uint8_t tag_list_1[MAX_BR_NUM];
  uint8_t enq_ptr_1;
  uint8_t deq_ptr_1;
  bool tag_vec_1[MAX_BR_NUM];
  int now_tag_1;
};
