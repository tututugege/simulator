#pragma once
#include "IO.h"
#include "config.h"
#include "frontend.h"
#include <cstdint>
#include <list>

#define NORMAL 0
#define MISPRED 1

class IDU_IO {
public:
  Front_Dec *front2dec;
  Dec_Front *dec2front;

  Dec_Ren *dec2ren;
  Ren_Dec *ren2dec;

  Dec_Broadcast *dec_bcast;

  Prf_Dec *prf2dec;
  Rob_Broadcast *rob_bc;
  Rob_Commit *commit;
};

class IDU {
  // 可能进入恢复分支状态

public:
  IDU_IO io;
  void init();
  void comb_decode(); // 译码并分配tag
  void comb_branch(); // 分支处理
  void comb_fire();   // 与前端握手
  void comb_flush();
  void comb_release_tag(); // 释放分支tag
  void seq();

  void io_gen_0(bool *input, bool *output);
  void io_gen_1(bool *input, bool *output);

  Inst_uop dec_uop[FETCH_WIDTH][2];
  bool dec_valid[FETCH_WIDTH];
  bool uop_valid[FETCH_WIDTH][2];

  int pop = false;
  bool push = false;  // 是否分配了新tag
  uint32_t alloc_tag; // 是否分配了新tag
  list<uint32_t> tag_list;

  bool tag_vec[MAX_BR_NUM];
  int now_tag;
  int state;

  bool tag_vec_1[MAX_BR_NUM];
  int now_tag_1;
  int state_1;
};
