#pragma once
#include <assert.h>
#include <cstdint>
using namespace std;
#define WAY 2
#define ARF_NUM 32
#define PRF_NUM 64
#define IQ_NUM 8
#define ROB_NUM 8

enum Inst_type {
  NOP,
  UTYPE,
  JTYPE,
  ITYPE,
  BTYPE,
  STYPE,
  RTYPE,
};

enum Inst_op {
  NONE,
  LUI,
  AUIPC,
  JAL,
  JALR,
  BEQ,
  BNE,
  BLT,
  BGE,
  BLTU,
  BGEU,
  LB,
  LH,
  LW,
  LBU,
  LHU,
  SB,
  SH,
  SW,
  ADD,
  SUB,
  SLL,
  SLT,
  SLTU,
  XOR,
  SRL,
  SRA,
  OR,
  AND
};

typedef struct Inst_info {
  int dest_idx, src1_idx, src2_idx;
  bool dest_en, src1_en, src2_en;
  Inst_type type;
  Inst_op op;
  uint32_t imm;
} Inst_info;

typedef struct Inst_res {
  uint32_t result;
  uint32_t pc_next;
  bool branch;
} Inst_res;
