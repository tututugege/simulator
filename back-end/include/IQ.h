#pragma once
#include <SRAM.h>
#include <config.h>
#include <vector>

typedef struct IQ_out {
  vector<Inst_info> inst;
  int pos_idx[INST_WAY]; // rob idx
  int valid[INST_WAY];   // rob idx
  bool full;
} IQ_out;

typedef struct IQ_in {
  bool valid[INST_WAY];
  bool pos_bit[INST_WAY];
  bool src1_ready[INST_WAY];
  bool src2_ready[INST_WAY];
  int pos_idx[INST_WAY]; // rob idx
  Inst_info inst[INST_WAY];
} IQ_in;

class IQ {
public:
  IQ(int entry_num, int out_num);
  void init();
  void comb(); // 仲裁
  void seq();  // 写入IQ
  IQ_in in;
  IQ_out out;

private:
  void alloc_IQ(int *);
  SRAM<Inst_info> entry;

  /*register*/
  vector<bool> valid;
  vector<bool> pos_bit; // rob位置信息 用于仲裁找出最老指令
  vector<int> pos_idx;
  vector<bool> src1_ready;
  vector<bool> src2_ready;

  vector<bool> valid_1;
  vector<bool> pos_bit_1;
  vector<int> pos_idx_1;
  vector<bool> src1_ready_1;
  vector<bool> src2_ready_1;

  int entry_num;
  int fu_num;
};
