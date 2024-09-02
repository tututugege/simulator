#pragma once
#include <assert.h>
#define WAY 2
#define ARF_NUM 32
#define PRF_NUM 64
#define IQ_NUM 8
#define ROB_NUM 8

enum Inst_type {
  NOP,
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
  ADDI,
  SLTI,
  SLTIU,
  XORI,
  ORI,
  ANDI,
  SLLI,
  SRLI,
  SRAI,
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
  int imm;
} Inst_info;
