#pragma once
#include "IO.h"
#include "config.h"

class BackTop;

class RenIn {
public:
  DecRenIO *dec2ren;
  DecBroadcastIO *dec_bcast;
  IssAwakeIO *iss_awake;
  PrfAwakeIO *prf_awake;
  DisRenIO *dis2ren;
  RobBroadcastIO *rob_bcast;
  RobCommitIO *rob_commit;
};

class RenOut {
public:
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
  void comb_select();
  void comb_rename(); // 重命名
  void comb_fire();
  void comb_wake();
  void comb_alloc(); // 分配寄存器
  void comb_branch();
  void comb_commit();
  void comb_pipeline();
  void comb_flush();
  void seq();

  RenIO get_hardware_io(); // 获取硬件级别 IO (Hardware Reference)

  // register
  InstEntry inst_r[FETCH_WIDTH];
  reg<7> arch_RAT[ARF_NUM + 1];
  reg<7> spec_RAT[ARF_NUM + 1];
  reg<7> RAT_checkpoint[MAX_BR_NUM][ARF_NUM + 1];
  reg<1> free_vec[Prf_NUM];
  reg<1> alloc_checkpoint[MAX_BR_NUM][Prf_NUM];
  reg<1> busy_table[Prf_NUM];
  reg<1> spec_alloc[Prf_NUM]; // 处于speculative状态分配的寄存器

  InstEntry inst_r_1[FETCH_WIDTH];
  wire<7> arch_RAT_1[ARF_NUM + 1];
  wire<7> spec_RAT_1[ARF_NUM + 1];
  wire<7> RAT_checkpoint_1[MAX_BR_NUM][ARF_NUM + 1];
  wire<1> free_vec_1[Prf_NUM];
  wire<1> alloc_checkpoint_1[MAX_BR_NUM][Prf_NUM];
  wire<1> busy_table_1[Prf_NUM];
  wire<1> spec_alloc_1[Prf_NUM]; // 处于speculative状态分配的寄存器
};
