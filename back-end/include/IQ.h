
#include "config.h"
typedef struct IQ_entry {
  bool pos_bit;
  int pos_idx;
  Inst_info inst;
  bool src1_ready;
  bool src2_ready;
} IQ_entry;

typedef struct IQ_out {
  int src1_preg_idx[WAY];
  int src2_preg_idx[WAY];
  int dest_preg_idx[WAY];
} IQ_out;

typedef struct IQ_in {
  bool pos_bit[WAY];
  bool src1_ready[WAY];
  bool src2_ready[WAY];
  int pos_idx[WAY]; // rob idx
  Inst_info inst[WAY];
} IQ_in;

class IQ {
public:
  void init();
  void IQ_add_inst();                  // 写入IQ
  Inst_info IQ_sel_inst(int *rob_idx); // 仲裁
  void IQ_awake(int dest_preg_idx);    // 唤醒
  IQ_in in;
  IQ_out out;

private:
  int alloc_IQ();
  IQ_entry entry[IQ_NUM];
};
