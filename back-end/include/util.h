#include "config.h"
#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)
#define FREQ_REG_NUM 6

inline bool is_branch(Inst_op op) { return op == BR || op == JUMP; }

inline bool is_CSR(Inst_op op) {
  return (op == CSR || op == MRET || op == ECALL || op == EBREAK);
}

inline bool is_load(Inst_op op) { return (op == LOAD); }
inline bool is_sta(Inst_op op) { return (op == STA); }
inline bool is_std(Inst_op op) { return (op == STD); }

inline bool is_page_fault(Inst_uop uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool orR(bool *in, int num) {
  bool out = false;
  for (int i = 0; i < num; i++)
    out = out || in[i];
  return out;
}

inline bool andR(bool *in, int num) {
  bool out = true;
  for (int i = 0; i < num; i++)
    out = out && in[i];
  return out;
}

extern int freq_reg[FREQ_REG_NUM];

inline bool reg_idx_cond(int reg_idx) {
  // return (reg_idx >= 10 && reg_idx <= 15) || reg_idx == 1 || reg_idx == 2;
  // return true;
  // return (reg_idx >= 10 && reg_idx <= 15);
  if (reg_idx == 0)
    return true;

  for (int i = 0; i < FREQ_REG_NUM; i++) {
    if (reg_idx == freq_reg[i])
      return true;
  }

  return false;
}
