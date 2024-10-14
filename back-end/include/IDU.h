#pragma once
#include "config.h"
typedef struct IDU_out {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
  bool ready[INST_WAY];
  Br_info br;
} IDU_out;

typedef struct IDU_in {
  bool valid[INST_WAY];
  bool *instruction[INST_WAY];

  Br_info br;
  bool free_valid[ISSUE_WAY];
  uint32_t free_tag[ISSUE_WAY];

  bool dis_fire[INST_WAY];

} IDU_in;

class IDU {
public:
  void init();
  void comb_dec(); // 译码
  void comb_fire();
  void seq();
  IDU_in in;
  IDU_out out;

private:
  bool tag_vec[MAX_BR_NUM];
  uint32_t tag_fifo[MAX_BR_NUM];
  int enq_ptr = 0;
  int deq_ptr = 0;
  int now_tag;

  bool tag_vec_1[MAX_BR_NUM];
  uint32_t tag_fifo_1[MAX_BR_NUM];
  int enq_ptr_1 = 0;
  int deq_ptr_1 = 0;
  int now_tag_1;

  int free_tag[INST_WAY];
};
