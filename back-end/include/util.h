#pragma once
#include "config.h"

#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)

inline bool is_branch(Inst_type type) {
  return type == BR || type == JALR || type == JAL;
}

inline bool is_store(Inst_uop uop) {
  return uop.type == STORE || uop.type == AMO && uop.amoop != LR;
}

inline bool is_load(Inst_uop uop) {
  return uop.type == LOAD || uop.type == AMO && uop.amoop != SC;
}

inline bool is_CSR(Inst_type type) {
  return (type == CSR || type == MRET || type == ECALL || type == EBREAK);
}

inline bool is_branch_uop(Inst_op op) { return op == UOP_BR || op == UOP_JUMP; }

inline bool is_CSR_uop(Inst_op op) {
  return (op == UOP_CSR || op == UOP_MRET || op == UOP_ECALL ||
          op == UOP_EBREAK);
}

inline bool is_sta_uop(Inst_op op) { return op == UOP_STA; }

inline bool is_std_uop(Inst_op op) { return op == UOP_STD; }

inline bool is_load_uop(Inst_op op) { return op == UOP_LOAD; }

inline bool is_page_fault(Inst_uop uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool is_exception(Inst_uop uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store ||
         uop.illegal_inst || uop.type == ECALL;
}

inline bool is_flush_inst(Inst_uop uop) {
  return uop.type == CSR || uop.type == ECALL || uop.type == MRET ||
         uop.type == SRET || uop.type == SFENCE_VMA || is_exception(uop) ||
         uop.type == EBREAK;
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
