#pragma once
#include <SRAM.h>
#include <config.h>
#include <vector>

enum IQ_TYPE { INT, LD, ST };

typedef struct IQ_out {
  vector<bool> valid;
  bool ready[INST_WAY];
  vector<Inst_info> inst;
} IQ_out;

typedef struct IQ_in {
  bool dis_fire[INST_WAY];
  bool valid[INST_WAY];
  Inst_info inst[INST_WAY];

  // 分支信息
  Br_info br;

  // store唤醒
  bool st_valid;
  uint32_t st_idx;

} IQ_in;

typedef struct IQ_entry {
  bool valid;
  Inst_info inst;
} IQ_entry;

class IQ {
public:
  IQ(int entry_num, int out_num, IQ_TYPE);
  void init();

  void wake_up(Inst_info *);
  void comb_deq();
  void comb_enq();
  void comb_alloc();
  void comb_2();
  void seq(); // 写入IQ
  IQ_in in;
  IQ_out out;

private:
  vector<IQ_entry> entry;
  vector<IQ_entry> entry_1;

  // register
  int enq_ptr;
  int enq_ptr_1;

  // 中间
  vector<int> issue_idx;

  // config
  int entry_num;
  int fu_num;
  IQ_TYPE type;
};

/*class ISU {*/
/**/
/*};*/
