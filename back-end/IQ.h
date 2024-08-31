
#include "config.h"
typedef struct IQ_entry {
  int op;
  int dest_idx;
  bool dest_en;

  int src1_idx;
  bool src1_en;
  bool src1_ready;

  int src2_idx;
  bool src2_en;
  bool src2_ready;
} IQ_entry;

typedef struct IQ_out {
  int src1_preg_idx[WAY];
  int src2_preg_idx[WAY];
  int dest_preg_idx[WAY];
} IQ_out;

typedef struct IQ_in {
  Inst_type op[WAY];
  int src1_preg_idx[WAY];
  int src1_preg_en[WAY];
  int src2_preg_idx[WAY];
  int src2_preg_en[WAY];
  int dest_preg_idx[WAY];
  int dest_preg_en[WAY];
} IQ_in;

class IQ {
public:
  void init();
  void IQ_add_inst();
  void IQ_sel_inst(); // select instuction to execute
  struct IQ_in in;
  struct IQ_out out;

private:
  int alloc_IQ();
  IQ_entry entry[IQ_NUM];
};
