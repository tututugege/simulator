#pragma once
#include <assert.h>
#include <cstdint>

using namespace std;

#define FETCH_WIDTH 4
#define COMMIT_WIDTH 4

#define MAX_SIM_TIME 10000000000
#define ISSUE_WAY IQ_NUM
#define MAX_UOP_NUM 3

#define ARF_NUM 32
#define PRF_NUM 128
#define MAX_BR_NUM 16

#define CSR_NUM 21

#define ROB_BANK_NUM 4
#define ROB_NUM 128
#define ROB_LINE_NUM (ROB_NUM / ROB_BANK_NUM)

#define STQ_NUM 16
#define ALU_NUM 2
#define BRU_NUM 2

#define LOG_START 0
#define LOG (0 && (sim_time >= LOG_START))
#define MEM_LOG (0 && (sim_time >= LOG_START))

extern long long sim_time;

#define CONFIG_DIFFTEST
// #define CONFIG_RUN_REF
#define CONFIG_BPU

#define UART_BASE 0x10000000

enum IQ_TYPE {
  IQ_INTM,
  IQ_INTD,
  IQ_LD,
  IQ_STA,
  IQ_STD,
  IQ_BR0,
  IQ_BR1,
  IQ_NUM
};

enum FU_TYPE { FU_ALU, FU_LSU, FU_BRU, FU_MUL, FU_DIV, FU_TYPE_NUM };

enum Inst_type {
  NONE,
  JAL,
  JALR,
  ADD,
  BR,
  LOAD,
  STORE,
  CSR,
  ECALL,
  EBREAK,
  SFENCE_VMA,
  MRET,
  SRET,
  MUL,
  DIV,
  AMO
};

enum Inst_op {
  UOP_JUMP,
  UOP_ADD,
  UOP_BR,
  UOP_LOAD,
  UOP_STA,
  UOP_STD,
  UOP_CSR,
  UOP_ECALL,
  UOP_EBREAK,
  UOP_SFENCE_VMA,
  UOP_MRET,
  UOP_SRET,
  UOP_MUL,
  UOP_DIV,
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
  uint32_t instruction;

  int dest_areg, src1_areg, src2_areg;
  int dest_preg, src1_preg, src2_preg;
  int old_dest_preg;
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
  uint32_t pre_sta_mask;
  uint32_t pre_std_mask;

  int uop_num;

  // page_fault
  bool page_fault_inst = false;
  bool page_fault_load = false;
  bool page_fault_store = false;

  // illegal
  bool illegal_inst = false;

  Inst_op op;
  Inst_type type;

  // 原子指令信息
  AMO_op amoop;

  bool difftest_skip;

  int64_t inst_idx;

  // ROB 信息
  int cmp_num;
} Inst_uop;

typedef struct Inst_entry {
  bool valid;
  Inst_uop uop;
} Inst_entry;

typedef struct {
  bool valid;
  uint32_t preg;
} Wake_info;
