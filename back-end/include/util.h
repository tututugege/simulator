#pragma once
#include "types.h"

#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)
// Custom Assert Macro to avoid WSL2 issues
#define Assert(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("\033[1;31mAssertion failed: %s, file %s, line %d\033[0m\n",      \
             #cond, __FILE__, __LINE__);                                       \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

inline int get_rob_line(uint32_t rob_idx) { return rob_idx / ROB_BANK_NUM; }
inline int get_rob_bank(uint32_t rob_idx) { return rob_idx % ROB_BANK_NUM; }
inline uint32_t make_rob_idx(uint32_t line, uint32_t bank) {
  return (line * ROB_BANK_NUM) | bank;
}

inline bool is_branch(InstType type) { return type == BR || type == JALR; }

inline bool is_store(InstUop uop) {
  return uop.type == STORE ||
         (uop.type == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_load(InstUop uop) {
  return uop.type == LOAD || (uop.type == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_CSR(InstType type) {
  return (type == CSR || type == MRET || type == ECALL || type == EBREAK);
}

inline bool is_branch_uop(UopType op) { return op == UOP_BR || op == UOP_JUMP; }

inline bool is_CSR_uop(UopType op) {
  return (op == UOP_CSR || op == UOP_MRET || op == UOP_ECALL ||
          op == UOP_EBREAK);
}

inline bool cmp_inst_age(InstUop inst1, InstUop inst2) {
  if (inst1.rob_flag == inst2.rob_flag) {
    return inst1.rob_idx > inst2.rob_idx;
  } else {
    return inst1.rob_idx < inst2.rob_idx;
  }
}

inline bool is_sta_uop(UopType op) { return op == UOP_STA; }

inline bool is_std_uop(UopType op) { return op == UOP_STD; }

inline bool is_load_uop(UopType op) { return op == UOP_LOAD; }

inline bool is_page_fault(InstUop uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool is_exception(InstUop uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store ||
         uop.illegal_inst || uop.type == ECALL;
}

inline bool is_flush_inst(InstUop uop) {
  return uop.type == CSR || uop.type == ECALL || uop.type == MRET ||
         uop.type == SRET || uop.type == SFENCE_VMA || is_exception(uop) ||
         uop.type == EBREAK || (uop.flush_pipe && is_load(uop));
}
