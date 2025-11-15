#pragma once
#include <assert.h>
#include <cstdint>
#include <stdio.h>
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
typedef uint16_t reg12_t;
typedef uint16_t reg16_t;
typedef uint32_t reg32_t;

using namespace std;

#define FETCH_WIDTH 4
#define COMMIT_WIDTH 4

#define MAX_SIM_TIME 1000000000
#define ISSUE_WAY IQ_NUM
#define MAX_UOP_NUM 3

#define ARF_NUM 32
#define PRF_NUM 96
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
#define CONFIG_BPU
#define CONFIG_PERF_COUNTER
// #define CONFIG_RUN_REF
// #define CONFIG_RUN_REF_PRINT

/*
 * 宽松的va2pa检查：
 * 允许 DUT 判定为 page fault，但是 REF 判定不为 
 * page fault 时，通过 DIFFTEST 并以 DUT 为准
 */
#define CONFIG_LOOSE_VA2PA 

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
  wire4_t src1_latency, src2_latency; // 非单周期指令唤醒的latency
  wire1_t src1_is_pc;
  wire1_t src2_is_imm;
  wire3_t func3;
  wire1_t func7_5;
  wire32_t imm; // 好像不用32bit 先用着
  wire32_t pc;  // 未来将会优化pc的获取
  wire4_t tag;
  wire12_t csr_idx;
  wire7_t rob_idx;
  wire4_t stq_idx;
  wire16_t pre_sta_mask;
  wire16_t pre_std_mask;

  // ROB 信息
  wire2_t uop_num;
  wire2_t cplt_num;
  wire1_t rob_flag; // 用于对比指令年龄

  // 异常信息
  wire1_t page_fault_inst = false;
  wire1_t page_fault_load = false;
  wire1_t page_fault_store = false;
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
  wire2_t latency;
} Wake_info;

class Perf_count {
public:
  bool perf_start = false;
  uint64_t cycle = 0;
  uint64_t commit_num = 0;

  uint64_t cache_access_num = 0;
  uint64_t cache_miss_num = 0;

  uint64_t cond_br_num = 0;
  uint64_t jalr_br_num = 0;
  uint64_t jal_br_num = 0;
  uint64_t call_br_num = 0;
  uint64_t ret_br_num = 0;

  uint64_t cond_mispred_num = 0;
  uint64_t jalr_mispred_num = 0;
  uint64_t jal_mispred_num = 0;
  uint64_t call_mispred_num = 0;
  uint64_t ret_mispred_num = 0;

  uint64_t jal_dir_mispred = 0;
  uint64_t jal_addr_mispred = 0;

  uint64_t jalr_dir_mispred = 0;
  uint64_t jalr_addr_mispred = 0;

  uint64_t cond_dir_mispred = 0;
  uint64_t cond_addr_mispred = 0;

  uint64_t call_dir_mispred = 0;
  uint64_t call_addr_mispred = 0;

  uint64_t ret_dir_mispred = 0;
  uint64_t ret_addr_mispred = 0;

  void perf_reset() {
    cycle = 0;
    commit_num = 0;
    // cache
    cache_access_num = 0;
    cache_miss_num = 0;

    // bpu
    cond_br_num = 0;
    jalr_br_num = 0;
    jal_br_num = 0;
    call_br_num = 0;
    ret_br_num = 0;

    cond_mispred_num = 0;
    jalr_mispred_num = 0;
    jal_mispred_num = 0;
    call_mispred_num = 0;
    ret_mispred_num = 0;

    jal_dir_mispred = 0;
    jal_addr_mispred = 0;

    jalr_dir_mispred = 0;
    jalr_addr_mispred = 0;

    cond_dir_mispred = 0;
    cond_addr_mispred = 0;

    call_dir_mispred = 0;
    call_addr_mispred = 0;

    ret_dir_mispred = 0;
    ret_addr_mispred = 0;
  }

  void perf_print() {
    printf("\033[1;32minstruction num: %ld\033[0m\n", commit_num);
    printf("\033[1;32mcycle       num: %ld\033[0m\n", cycle);
    printf("\033[1;32mipc            : %f\033[0m\n",
           (double)commit_num / cycle);
    printf("\n");
    perf_print_cache();
    perf_print_branch();
  }

  void perf_print_cache() {
    printf("\033[1;32m*********CACHE COUNTER************\033[0m\n");

    printf("\033[1;32mcache accuracy : %f\033[0m\n",
           1 - cache_miss_num / (double)cache_access_num);
    printf("\033[1;32mcache access   : %ld\033[0m\n", cache_access_num);
    printf("\033[1;32mcache hit      : %ld\033[0m\n",
           cache_access_num - cache_miss_num);
    printf("\033[1;32mcache miss     : %ld\033[0m\n", cache_miss_num);
    printf("\n");
  }

  void perf_print_branch() {
    printf("\033[1;32m*********BPU COUNTER************\033[0m\n");
    printf("\033[1;32mbpu   accuracy : %f\033[0m\n\n",
           1 - (cond_mispred_num + jalr_mispred_num + jal_mispred_num +
                call_mispred_num + ret_mispred_num) /
                   (double)(cond_br_num + jalr_br_num + jal_br_num +
                            call_br_num + ret_br_num));

    printf("\033[1;32mjal   accuracy : %f\033[0m\n",
           1 - (jal_mispred_num) / (double)(jal_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", jal_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", jal_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", jal_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", jal_dir_mispred);
    printf("\n");

    printf("\033[1;32mjalr  accuracy : %f\033[0m\n",
           1 - (jalr_mispred_num) / (double)(jalr_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", jalr_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", jalr_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", jalr_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", jalr_dir_mispred);
    printf("\n");

    printf("\033[1;32mbr    accuracy : %f\033[0m\n",
           1 - (cond_mispred_num) / (double)(cond_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", cond_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", cond_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", cond_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", cond_dir_mispred);
    printf("\n");

    printf("\033[1;32mcall  accuracy : %f\033[0m\n",
           1 - (call_mispred_num) / (double)(call_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", call_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", call_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", call_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", call_dir_mispred);
    printf("\n");

    printf("\033[1;32mret    accuracy : %f\033[0m\n",
           1 - (ret_mispred_num) / (double)(ret_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", ret_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", ret_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", ret_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", ret_dir_mispred);
    printf("\n");
  }
};

extern Perf_count perf;
