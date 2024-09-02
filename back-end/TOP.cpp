#include "TOP.h"

void Back_Top::init() {
  rename.init();
  iq.init();
  rob.init();
}

void Back_Top::Back_cycle() {
  // 组合逻辑
  // pipeline1: 重命名 dispatch

  for (int i = 0; i < WAY; i++) {
    rename.in.src1_areg_idx[i] = in.inst[i].src1_idx;
    rename.in.src1_areg_en[i] = in.inst[i].src1_en;
    rename.in.src2_areg_idx[i] = in.inst[i].src2_idx;
    rename.in.src2_areg_en[i] = in.inst[i].src2_en;
    rename.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rename.in.dest_areg_en[i] = in.inst[i].dest_en;
  }
  rename.cycle();

  // pipeline2: 从IQ中选择指令执行
  Inst_info inst[WAY];
  for (int i = 0; i < WAY; i++) {
    inst[i] = iq.IQ_sel_inst();
  }

  // pipeline3: ROB提交指令

  // 时序逻辑
  // pipeline1: 写入ROB和IQ
  for (int i = 0; i < WAY; i++) {
    rob.in.PC[i] = in.PC[i];
    rob.in.type[i] = in.inst[i].type;
    rob.in.dest_preg_idx[i] = rename.out.dest_preg_idx[i];
    rob.in.old_dest_preg_idx[i] = rename.out.old_dest_preg_idx[i];
  }
  rob.ROB_enq(iq.in.pos_bit, iq.in.pos_idx);

  for (int i = 0; i < WAY; i++) {
    iq.in.inst[i].type = in.inst[i].type;
    iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    iq.in.inst[i].src1_en = rename.in.src1_areg_en[i];
    iq.in.inst[i].src2_idx = rename.out.src2_preg_idx[i];
    iq.in.inst[i].src2_en = rename.in.src2_areg_en[i];
    iq.in.inst[i].dest_idx = rename.out.dest_preg_idx[i];
    iq.in.inst[i].dest_en = rename.in.dest_areg_en[i];
  }
  iq.IQ_add_inst();

  for (int i = 0; i < WAY; i++) {
  }
  // pipeline2: 执行结果写回 唤醒

  for (int i = 0; i < WAY; i++) {
    if (inst[i].dest_en)
      iq.IQ_awake(inst[i].dest_idx);
  }

  // pipeline3: ROB提交
}
