#pragma once
#include <assert.h>
#include <cstdint>

using namespace std;

#define MAX_SIM_TIME 500000
#define ISSUE_WAY 4

#define ARF_NUM 32
#define PRF_NUM 96
#define MAX_BR_NUM 8

#define ROB_NUM 64
#define STQ_NUM 8
#define ALU_NUM 2

#define LOG 0

#define CONFIG_DIFFTEST
/*#define CONFIG_BRANCHCHECK*/
/*#define CONFIG_BPU*/

#define UART_BASE 0x10000000

enum IQ_TYPE { IQ_INTM, IQ_INTD, IQ_LS, IQ_BR, IQ_NUM };
enum FU_TYPE { FU_ALU, FU_LSU, FU_BRU, FU_MUL, FU_DIV, FU_TYPE_NUM };

extern int fu_config[ISSUE_WAY];

enum Inst_op {
  NONE,
  JUMP,
  ADD,
  BR,
  LOAD,
  STORE,
  CSR,
  ECALL,
  EBREAK,
  MRET,
  MUL,
  DIV
};

enum AMO_op {
  AMONONE,
  LR,
  SC,
  AMOSWAP,
  AMOADD,
  AMOXOR,
  AMOAND,
  AMOOR,
  AMOMIN,
  AMOMAX,
  AMOMINU,
  AMOMAXU,
};

typedef struct Inst_uop {
  int dest_areg, src1_areg, src2_areg;
  int dest_preg, src1_preg, src2_preg;
  uint32_t src1_rdata, src2_rdata;
  uint32_t result;

  // 分支预测信息
  bool pred_br_taken;
  bool alt_pred;
  uint8_t altpcpn;
  uint8_t pcpn;
  uint32_t pred_br_pc;

  // 分支预测更新信息
  bool mispred;
  bool br_taken;
  uint32_t pc_next;
  int old_dest_preg;

  bool dest_en, src1_en, src2_en;
  bool src1_busy, src2_busy;
  bool src1_is_pc;
  bool src2_is_imm;
  uint32_t func3;
  bool func7_5;
  uint32_t imm;
  uint32_t pc;
  uint32_t tag;
  uint32_t csr_idx;
  uint32_t rob_idx;
  uint32_t stq_idx;
  bool pre_store[STQ_NUM];

  bool is_last_uop;

  // 调度特征
  int inst_idx;
  int dependency;

  Inst_op op;
  IQ_TYPE iq_type;

  // 原子指令信息
  AMO_op amoop;
} Inst_uop;

typedef struct Inst_entry {
  bool valid;
  Inst_uop uop;
} Inst_entry;

typedef struct {
  bool valid;
  uint32_t preg;
} Wake_info;

/*typedef struct Inst_res {*/
/*  uint32_t result;*/
/*  uint32_t pc_next;*/
/*  bool branch;*/
/*} Inst_res;*/
