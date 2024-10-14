#pragma once
#include <assert.h>
#include <cstdint>
#include <iostream>
using namespace std;

#define INST_WAY 2
#define ISSUE_WAY ALU_NUM + AGU_NUM
#define ALU_NUM 2
#define AGU_NUM 2
#define BRU_NUM 2

#define ARF_NUM 32
#define PRF_NUM 64
#define PRF_WR_NUM 3
#define PRF_RD_NUM 7
#define PRF_WR_LD_PORT 3

#define MAX_BR_NUM 4

#define IQ_NUM 8
#define ROB_NUM 8

#define LDQ_NUM 8

#define LOG 1

#define CONFIG_DIFFTEST

enum Inst_type {
  INVALID,
  UTYPE,
  JTYPE,
  ITYPE,
  BTYPE,
  STYPE,
  RTYPE,
};

enum Inst_op { NONE, LUI, AUIPC, JAL, JALR, ADD, BR, LOAD, STORE };

typedef struct Inst_info {
  int dest_areg, src1_areg, src2_areg;
  int dest_preg, src1_preg, src2_preg;
  int old_dest_preg;
  bool dest_en, src1_en, src2_en;
  bool src1_busy, src2_busy;
  Inst_op op;
  bool src2_is_imm;
  uint32_t func3;
  bool func7_5;
  uint32_t imm;
  uint32_t pc;
  uint32_t tag;
  uint32_t rob_idx;
  int pc_next;
} Inst_info;

typedef struct {
  uint32_t br_tag;
  bool br_mask[MAX_BR_NUM];
  bool br_taken;
} Br_info;

typedef struct {
  bool valid;
  uint32_t preg;
} Wake_info;

inline bool is_branch(Inst_op op) {
  return op == BR || op == JALR || op == JAL;
}

/*typedef struct Inst_res {*/
/*  uint32_t result;*/
/*  uint32_t pc_next;*/
/*  bool branch;*/
/*} Inst_res;*/
