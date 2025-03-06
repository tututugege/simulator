#pragma once
#include "config.h"
#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)

inline bool is_branch(Inst_info inst) {

  return (inst.op == BR || inst.op == JALR || inst.op == JAL);
}

inline bool is_CSR(Inst_info inst) {

  return (inst.op == CSR || inst.op == MRET || inst.op == ECALL ||
          inst.op == EBREAK);
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

enum mem_sz_t {
  BYTE,
  HALF,
  WORD,
};

enum src_t { RS, LDQ, STQ };

enum op_t { OP_LD, OP_ST };

struct lsq_alloc_req {
  op_t op_in;
  mem_sz_t mem_sz_in;
  int dst_preg_in;
  bool sign_in;
  int lsq_entry_out;
  bool valid_in;
  bool ready_in;
};
