#pragma once
#include "IO.h"
#include "config.h"

class BackTop;

struct DisOut {
  DisRenIO *dis2ren;
  DisRobIO *dis2rob;
  DisIssIO *dis2iss;
  DisLsuIO *dis2lsu;
};

struct DisIn {
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
  int iq_id;               // 目标 IQ
  DisIssIO::DisIssUop uop; // Dispatch 内部直接使用发往 Issue 的线网格式
};

class Dispatch {
private:
  struct DispatchInst : public RenDisIO::RenDisInst {
    wire<1> mispred;
    wire<1> br_taken;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<STQ_IDX_WIDTH> stq_idx;
    wire<1> stq_flag;
    wire<LDQ_IDX_WIDTH> ldq_idx;
    wire<1> rob_flag;
    wire<1> flush_pipe;

    DispatchInst()
        : mispred(0), br_taken(0), rob_idx(0), stq_idx(0), stq_flag(0),
          ldq_idx(0), rob_flag(0), flush_pipe(0) {}

    static DispatchInst from_ren_dis_inst(const RenDisIO::RenDisInst &src) {
      DispatchInst dst;
      static_cast<RenDisIO::RenDisInst &>(dst) = src;
      return dst;
    }

    DisRobIO::DisRobInst to_dis_rob_inst() const {
      DisRobIO::DisRobInst dst;
      dst.diag_val = diag_val;
      dst.dest_areg = dest_areg;
      dst.src1_areg = src1_areg;
      dst.dest_preg = dest_preg;
      dst.old_dest_preg = old_dest_preg;
      dst.ftq_idx = ftq_idx;
      dst.ftq_offset = ftq_offset;
      dst.ftq_is_last = ftq_is_last;
      dst.mispred = mispred;
      dst.br_taken = br_taken;
      dst.dest_en = dest_en;
      dst.is_atomic = is_atomic;
      dst.func3 = func3;
      dst.func7 = func7;
      dst.imm = imm;
      dst.br_mask = br_mask;
      dst.rob_idx = rob_idx;
      dst.stq_idx = stq_idx;
      dst.stq_flag = stq_flag;
      dst.ldq_idx = ldq_idx;
      dst.expect_mask = expect_mask;
      dst.cplt_mask = cplt_mask;
      dst.rob_flag = rob_flag;
      dst.page_fault_inst = page_fault_inst;
      dst.illegal_inst = illegal_inst;
      dst.type = type;
      dst.tma = tma;
      dst.dbg = dbg;
      dst.flush_pipe = flush_pipe;
      return dst;
    }
  };

  DisIssIO::DisIssUop make_dis_iss_uop(const DispatchInst &inst) const;
  bool is_preg_woken(wire<PRF_IDX_WIDTH> preg) const;
  void apply_wakeup_to_uop(RenDisIO::RenDisInst &uop) const;
  int decompose_inst(const DispatchInst &original_inst, UopPacket *out_uops);

  DispatchInst inst_alloc[DECODE_WIDTH];

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
  DisIn in;
  DisOut out;

  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）
  void comb_alloc();
  void comb_dispatch();
  void comb_wake();
  void comb_fire();
  void comb_pipeline();
  void seq();
  RenDisIO::RenDisInst inst_r[DECODE_WIDTH];
  RenDisIO::RenDisInst inst_r_1[DECODE_WIDTH];
  reg<1> inst_valid[DECODE_WIDTH];
  wire<1> inst_valid_1[DECODE_WIDTH];
  reg<1> busy_table[PRF_NUM];
  wire<1> busy_table_1[PRF_NUM];
};
