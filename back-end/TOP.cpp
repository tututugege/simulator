#include "IQ.h"
#include "ROB.h"
#include "Rename.h"
#include "config.h"

typedef struct Back_in {
  Inst_type type[WAY];
  int src1_areg_idx[WAY];
  int src1_areg_en[WAY];
  int src2_areg_idx[WAY];
  int src2_areg_en[WAY];
  int dest_areg_idx[WAY];
  int dest_areg_en[WAY];
} Back_in;

class Back_Top {
  Rename rename;
  IQ iq;
  ROB rob;
  Back_in in;
  void Back_cycle();
  void init();
};

void Back_Top::init() {
  rename.init();
  iq.init();
  rob.init();
}

void Back_Top::Back_cycle() {
  // 组合逻辑
  // pipeline1: 重命名

  for (int i = 0; i < WAY; i++) {
    rename.in.src1_areg_idx[i] = in.src1_areg_idx[i];
    rename.in.src1_areg_en[i] = in.src1_areg_en[i];
    rename.in.src2_areg_idx[i] = in.src2_areg_idx[i];
    rename.in.src2_areg_en[i] = in.src2_areg_en[i];
    rename.in.dest_areg_idx[i] = in.dest_areg_idx[i];
    rename.in.dest_areg_en[i] = in.dest_areg_en[i];
  }
  rename.cycle();

  // pipeline2: 从IQ中选择指令执行

  // pipeline3: ROB提交指令

  // 时序逻辑
  // pipeline1: 写入ROB和IQ
  for (int i = 0; i < WAY; i++) {
    iq.in.op[i] = in.type[i];
    iq.in.src1_preg_idx[i] = rename.out.src1_preg_idx[i];
    iq.in.src1_preg_en[i] = rename.in.src1_areg_en[i];
    iq.in.src2_preg_idx[i] = rename.out.src2_preg_idx[i];
    iq.in.src2_preg_en[i] = rename.in.src2_areg_en[i];
    iq.in.dest_preg_idx[i] = rename.out.dest_preg_idx[i];
    iq.in.dest_preg_en[i] = rename.in.dest_areg_en[i];
  }
  iq.IQ_add_inst();

  for (int i = 0; i < WAY; i++) {
  }
  rob.ROB_enq();
  // pipeline2: 执行结果写回
  // pipeline3: ROB提交
}
