#pragma once
#include <SRAM.h>
#include <config.h>
#include <vector>

typedef struct IQ_out {
  // 握手信号
  int valid[INST_WAY];
  int ready[INST_WAY];
  bool all_ready;

  vector<Inst_info> inst;
} IQ_out;

typedef struct IQ_in {
  // 握手信号
  bool valid[INST_WAY];
  bool ready[ISSUE_WAY];
  bool all_ready;

  Inst_info inst[INST_WAY];

  // 分支信息
  Br_info br;

} IQ_in;

typedef struct IQ_entry {
  bool valid;
  Inst_info inst;
  bool src1_busy;
  bool src2_busy;
} IQ_entry;

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
  vector<IQ_entry> entry;
  vector<IQ_entry> entry_1;

  // config
  int entry_num;
  int fu_num;
};
