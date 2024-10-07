#pragma once
#include <SRAM.h>
#include <config.h>

typedef struct ROB_in {

  // dispatch写入ROB
  bool from_ren_valid[INST_WAY];
  Inst_info inst[INST_WAY];

  // execute完成情况
  bool from_ex_valid[ISSUE_WAY];
  int rob_idx[ISSUE_WAY];

  // 分支信息
  Br_info br;
} ROB_in;

typedef struct ROB_out {
  // 流水线握手信号
  bool to_ex_ready[ISSUE_WAY];
  bool to_ren_ready[INST_WAY];
  bool to_ex_all_ready;
  bool to_ren_all_ready;

  int enq_idx;
  Inst_info commit_entry[ISSUE_WAY];
  bool valid[ISSUE_WAY];
} ROB_out;

class ROB {
public:
  void init();
  void seq();
  void comb();

  ROB_in in;
  ROB_out out;

private:
  SRAM<Inst_info> entry = SRAM<Inst_info>(3, 2, ROB_NUM, sizeof(Inst_info) * 8);

  bool valid[ROB_NUM];
  bool complete[ROB_NUM];
  int enq_ptr;
  int deq_ptr;
  int count;
  uint32_t tag[ROB_NUM];

  bool valid_1[ROB_NUM];
  bool complete_1[ROB_NUM];
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;
  uint32_t tag_1[ROB_NUM];
};
