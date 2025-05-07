#include "config.h"
#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)

inline bool is_branch(Inst_op op) {
  return op == BR || op == JALR || op == JAL;
}

inline bool is_CSR(Inst_op op) {

  return (op == CSR || op == MRET || op == ECALL || op == EBREAK);
}

inline bool is_load(Inst_op op) {
  return (op == LOAD || op == LR || op == AMO);
}

inline bool is_store(Inst_op op) {
  return (op == STORE || op == SC || op == AMO);
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
