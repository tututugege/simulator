#pragma once
#include <SRAM.h>
#include <config.h>
#include <vector>

typedef struct IQ_out {
  // 握手信号
  int valid[INST_WAY]; // rob idx
  int ready[INST_WAY]; // rob idx

  vector<Inst_info> inst;
  int pos_idx[INST_WAY]; // rob idx
  bool full;

} IQ_out;

typedef struct IQ_in {
  // 握手信号
  bool valid[INST_WAY];
  bool ready[ISSUE_WAY];
  bool all_ready;

  bool pos_bit[INST_WAY];
  bool src1_ready[INST_WAY];
  bool src2_ready[INST_WAY];
  int pos_idx[INST_WAY]; // rob idx
  Inst_info inst[INST_WAY];

  bool br_taken;
  uint32_t br_tag;
} IQ_in;

/*typedef struct IQ_entry {*/
/*  int dest_idx;*/
/*  bool dest_en;*/
/*  Inst_op op;*/
/*  uint32_t imm;*/
/*  uint32_t pc;*/
/*} IQ_entry;*/

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
  vector<uint32_t> tag;
  vector<uint32_t> src1_preg_idx;
  vector<uint32_t> src2_preg_idx;
  vector<bool> src1_ready;
  vector<bool> src2_ready;
  int count;

  vector<bool> valid_1;
  vector<bool> pos_bit_1;
  vector<int> pos_idx_1;
  vector<uint32_t> tag_1;
  vector<uint32_t> src1_preg_idx_1;
  vector<uint32_t> src2_preg_idx_1;
  vector<bool> src1_ready_1;
  vector<bool> src2_ready_1;
  int count_1;

  int entry_num;
  int fu_num;
};
