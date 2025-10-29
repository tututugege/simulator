#pragma once
#include <assert.h>
#include <cstdint>
typedef bool wire1_t;
typedef uint8_t wire2_t;
typedef uint8_t wire3_t;
typedef uint8_t wire4_t;
typedef uint8_t wire5_t;
typedef uint8_t wire6_t;
typedef uint8_t wire7_t;
typedef uint8_t wire8_t;
typedef uint16_t wire12_t;
typedef uint16_t wire16_t;
typedef uint32_t wire32_t;

typedef bool reg1_t;
typedef uint8_t reg2_t;
typedef uint8_t reg3_t;
typedef uint8_t reg4_t;
typedef uint8_t reg5_t;
typedef uint8_t reg6_t;
typedef uint8_t reg7_t;
typedef uint8_t reg8_t;
typedef uint16_t reg16_t;
typedef uint32_t reg32_t;

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

#define LOG_START 8643800
#define LOG (0 && (sim_time >= LOG_START))
#define MEM_LOG (0 && (sim_time >= LOG_START))

extern long long sim_time;

// #define CONFIG_DIFFTEST
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
  wire32_t instruction;

  wire5_t dest_areg, src1_areg, src2_areg;
  wire7_t dest_preg, src1_preg, src2_preg; // log2(ROB_NUM)
  wire7_t old_dest_preg;
  wire32_t src1_rdata, src2_rdata;
  wire32_t result;

  // 分支预测信息
  wire1_t pred_br_taken;
  wire1_t alt_pred;
  wire8_t altpcpn;
  wire8_t pcpn;
  wire32_t pred_br_pc;

  // 分支预测更新信息
  wire1_t mispred;
  wire1_t br_taken;
  wire32_t pc_next;

  wire1_t dest_en, src1_en, src2_en;
  wire1_t src1_busy, src2_busy;
  wire1_t src1_is_pc;
  wire1_t src2_is_imm;
  wire3_t func3;
  wire1_t func7_5;
  wire32_t imm; // 好像不用32bit 先用着
  wire32_t pc;
  wire4_t tag;
  wire12_t csr_idx;
  wire7_t rob_idx;
  wire4_t stq_idx;
  wire16_t pre_sta_mask;
  wire16_t pre_std_mask;

  // ROB 信息
  wire2_t uop_num;
  wire2_t cmp_num;

  // page_fault
  wire1_t page_fault_inst = false;
  wire1_t page_fault_load = false;
  wire1_t page_fault_store = false;

  // illegal
  wire1_t illegal_inst = false;

  Inst_type type;
  Inst_op op;
  AMO_op amoop;

  // for debug
  bool difftest_skip;
  int64_t inst_idx;
} Inst_uop;

typedef struct Inst_entry {
  wire1_t valid;
  Inst_uop uop;
} Inst_entry;

typedef struct {
  wire1_t valid;
  wire7_t preg;
} Wake_info;
