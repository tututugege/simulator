#pragma once
#include <assert.h>
#include <cstdint>
#include <vector>
using namespace std;

#define MAX_SIM_TIME 10
#define INST_WAY 2
#define ISSUE_WAY 6
#define ALU_NUM 4
#define AGU_NUM 2
#define BRU_NUM 2
#define EXU_NUM 6

#define ARF_NUM 32
#define PRF_NUM 64
#define PRF_WR_NUM 3
#define PRF_RD_NUM 7
#define PRF_WR_LD_PORT 2

#define MAX_BR_NUM 4

#define IQ_NUM 8
#define ROB_NUM 8

#define LDQ_NUM 4
#define STQ_NUM 4

#define LOG 0

#define CONFIG_DIFFTEST

#define UART_BASE 0x10000000

enum IQ_TYPE { IQ_INT, IQ_MEM, IQ_CSR };
enum FU_TYPE { FU_ALU, FU_CSR, FU_AGU };
enum Sched_type { OLDEST_FIRST, INDEX, IN_ORDER, DEPENDENCY, GREEDY };

extern vector<vector<FU_TYPE>> fu_config;

enum Inst_op {
  NONE,
  LUI,
  AUIPC,
  JAL,
  JALR,
  ADD,
  BR,
  LOAD,
  STORE,
  CSR,
  ECALL,
  EBREAK,
  MRET
};

typedef struct Inst_info {
  int dest_areg, src1_areg, src2_areg;
  int dest_preg, src1_preg, src2_preg;
  uint32_t src1_rdata, src2_rdata;
  uint32_t result;
  bool mispred;
  bool pred_br_taken;
  uint32_t pred_br_pc;
  int old_dest_preg;
  IQ_TYPE iq_type;
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
  uint32_t stq_idx;
  int pc_next;
  bool pre_store[STQ_NUM];
  uint32_t csr_idx;
  uint32_t ptab_idx;

  // 调度特征
  int inst_idx;
  int dependency;

} Inst_info;

typedef struct Inst_entry {
  bool valid;
  Inst_info inst;
} Inst_entry;

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
