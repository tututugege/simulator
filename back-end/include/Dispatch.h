#pragma once
#include "IO.h"
#include "config.h"

class BackTop;

class DIS_OUT {
public:
  DisRenIO *dis2ren;
  DisRobIO *dis2rob;
  DisIssIO *dis2iss;
  DisLsuIO *dis2lsu;
};

class DIS_IN {
public:
  RenDisIO *ren2dis;
  RobDisIO *rob2dis;
  IssDisIO *iss2dis;
  LsuDisIO *lsu2dis;
  PrfAwakeIO *prf_awake;
  IssAwakeIO *iss_awake;
  RobBroadcastIO *rob_bcast;
  DecBroadcastIO *dec_bcast;
};

struct UopPacket {
  int iq_id;   // 目标 IQ
  MicroOp uop; // 微操作内容
};

class Dispatch {
private:
  int decompose_inst(const InstEntry &original_inst, UopPacket *out_uops);
  bool is_preg_woken(wire<PRF_IDX_WIDTH> preg) const;
  void apply_wakeup_to_uop(InstInfo &uop) const;
  void refresh_source_busy();

  InstEntry inst_alloc[DECODE_WIDTH];
  reg<1> busy_table[PRF_NUM];
  wire<1> busy_table_1[PRF_NUM];

  // 记录每条指令 Dispatch 是否成功 (comb_dispatch -> comb_fire)
  bool dispatch_success_flags[DECODE_WIDTH];

  // 记录每个端口分配到的指令槽位，-1 表示该端口本拍未分配。
  int stq_port_owner[MAX_STQ_DISPATCH_WIDTH];
  int ldq_port_owner[MAX_LDQ_DISPATCH_WIDTH];

  struct DispatchCache {
    int count;                     // 拆分数量
    int iq_ids[MAX_UOPS_PER_INST]; // 仅保存目标 IQ 的 ID
  };

  // 用于在 comb_dispatch 和 comb_fire 之间传递数据
  DispatchCache dispatch_cache[DECODE_WIDTH];

public:
  Dispatch(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  DIS_IN in;
  DIS_OUT out;

  void init();
  void comb_alloc();
  void comb_dispatch();
  void comb_wake();
  void comb_fire();
  void comb_pipeline();
  void seq();

  DispatchIO get_hardware_io(); // Hardware Reference
  InstEntry inst_r[DECODE_WIDTH];
  InstEntry inst_r_1[DECODE_WIDTH];
};
