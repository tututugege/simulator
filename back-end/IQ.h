
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
  int pos_idx[WAY];
  Inst_info inst[WAY];
} IQ_in;

class IQ {
public:
  void init();
  void IQ_add_inst();
  Inst_info IQ_sel_inst();          // 仲裁
  void IQ_awake(int dest_preg_idx); // 唤醒
  struct IQ_in in;
  struct IQ_out out;

private:
  int alloc_IQ();
  IQ_entry entry[IQ_NUM];
};
