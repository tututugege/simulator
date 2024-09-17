#pragma once
#include "config.h"
typedef struct IQ_entry {
  bool pos_bit;
  int pos_idx;
  Inst_info inst;
  bool src1_ready;
  bool src2_ready;
} IQ_entry;

typedef struct IQ_out {
  IQ_entry int_entry[ALU_NUM];
  IQ_entry mem_entry[AGU_NUM];
  bool full;
} IQ_out;

typedef struct IQ_in {
  bool pos_bit[INST_WAY];
  bool src1_ready[INST_WAY];
  bool src2_ready[INST_WAY];
  int pos_idx[INST_WAY]; // rob idx
  Inst_info inst[INST_WAY];
} IQ_in;

enum Issue_type { INT, MEM };

class IQ {
public:
  void init();
  void comb(); // 仲裁
  void seq();  // 写入IQ
  /*void IQ_awake(int dest_preg_idx); // 唤醒*/
  IQ_in in;
  IQ_out out;

private:
  int alloc_IQ();
  IQ_entry entry[IQ_NUM];

  int oldest_i[ALU_NUM];
  int oldest_i_mem = -1;
};
