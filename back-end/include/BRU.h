#pragma once
#include <FIFO.h>
#include <config.h>

typedef struct BRU_in {
  uint32_t pc;
  uint32_t off;
  uint32_t src1;
  bool alu_out;
  Inst_op op;
} BRU_in;

typedef struct BRU_out {
  uint32_t pc_next;
  bool br_taken;
  bool idle;
} BRU_out;

class BRU {
public:
  void cycle();
  BRU_in in;
  BRU_out out;
};

typedef struct Br_Tag_in {
  bool valid[INST_WAY];

  bool free_valid[MAX_BR_NUM - 1];
  uint32_t free_tag[MAX_BR_NUM - 1];
} Br_Tag_in;

typedef struct Br_Tag_out {
  uint32_t tag[INST_WAY];
  bool ready[INST_WAY];
} Br_Tag_out;

class Br_Tag {
public:
  Br_Tag_in in;
  Br_Tag_out out;
  void init();
  void comb();
  void seq();

  bool tag_vec[MAX_BR_NUM];
  uint32_t tag_fifo[MAX_BR_NUM];
  int enq_ptr = 0;
  int deq_ptr = 0;
  int last_tag;

  bool tag_vec_1[MAX_BR_NUM];
  uint32_t tag_fifo_1[MAX_BR_NUM];
  int enq_ptr_1 = 0;
  int deq_ptr_1 = 0;
  int last_tag_1;
};
