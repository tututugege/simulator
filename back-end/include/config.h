#pragma once
#include <assert.h>
#include <cstdint>

using namespace std;

#define MAX_SIM_TIME 2000000
#define ISSUE_WAY 7

#define ARF_NUM 32
#define PRF_NUM 64
#define MAX_BR_NUM 4

#define IQ_NUM 4
#define ROB_NUM 32
#define STQ_NUM 8

#define LOG 0

#define CONFIG_DIFFTEST
/*#define CONFIG_BRANCHCHECK*/
#define CONFIG_BPU

#define UART_BASE 0x10000000

enum IQ_TYPE { IQ_INT, IQ_BR, IQ_LD, IQ_ST };
enum FU_TYPE { FU_ALU, FU_BRU, FU_LDU, FU_STU, FU_NUM };

extern FU_TYPE fu_config[ISSUE_WAY];

enum Inst_op { NONE, LUI, AUIPC, JAL, JALR, ADD, BR, LOAD, STORE, EBREAK };

typedef struct Inst_info {
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
  bool pre_store[STQ_NUM];

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
