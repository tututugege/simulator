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

// typedef wire4_t Inst_op;
// typedef wire4_t Inst_type;
// typedef wire4_t Amo_op;

typedef wire5_t tag_t;
typedef wire5_t stq_t;
typedef wire8_t rob_t;
typedef wire32_t brmask_t;

using namespace std;
#define VIRTUAL_MEMORY_LENGTH (1024 * 1024 * 1024)  // 4B
#define PHYSICAL_MEMORY_LENGTH (1024 * 1024 * 1024) // 4B

#define MAX_SIM_TIME 100000000000

#define FETCH_WIDTH 8
#define COMMIT_WIDTH 8

#define ISSUE_WAY IQ_NUM

#define ARF_NUM 32
#define PRF_NUM 128
#define MAX_BR_NUM 32

#define CSR_NUM 21

#define ROB_NUM (ROB_BANK_NUM * ROB_LINE_NUM)
#define ROB_BANK_NUM 8
#define ROB_LINE_NUM 32 // (ROB_NUM / ROB_BANK_NUM)

#define STQ_NUM 32
#define ALU_NUM 4
#define LDU_NUM 2
#define STA_NUM 2
#define STD_NUM 2
#define BRU_NUM 2

#define LOG_START 0
#define LOG 1
#define MEM_LOG 0

extern long long sim_time;

// #define HAS_MMU

#define CONFIG_DIFFTEST
#define CONFIG_BPU
#define CONFIG_PERF_COUNTER

// #define CONFIG_RUN_REF
// #define CONFIG_RUN_CKPT
#define SIMPOINT_INTERVAL 100000000
#define WARMUP 100000000

#define CONFIG_PERF_COUNTER
#define CONFIG_BPU
// #define CONFIG_MMU
#define ENABLE_MULTI_BR

/*
 * 宽松的va2pa检查：
 * 允许 DUT 判定为 page fault，但是 REF 判定不为
 * page fault 时，通过 DIFFTEST 并以 DUT 为准
 */
#define CONFIG_LOOSE_VA2PA

#define UART_BASE 0x10000000

enum IQType {
  IQ_INTM,
  IQ_INTD,
  IQ_INT0,
  IQ_INT1,
  IQ_LD0,
  IQ_LD1,
  IQ_STA0,
  IQ_STA1,
  IQ_STD0,
  IQ_STD1,
  IQ_BR0,
  IQ_BR1,
  IQ_NUM
};

enum InstType {
  NOP,
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
  AMO,
};

enum UopType {
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

#define AMONONE 0
#define LR 1
#define SC 2
#define AMOSWAP 3
#define AMOADD 4
#define AMOXOR 5
#define AMOAND 6
#define AMOOR 7
#define AMOMIN 8
#define AMOMAX 9
#define AMOMINU 10
#define AMOMAXU 11

typedef struct Inst_uop {
  wire32_t instruction;

  wire6_t dest_areg, src1_areg, src2_areg;
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
  wire32_t tage_idx[4]; // TN_MAX = 4

  // 分支预测更新信息
  wire1_t mispred;
  wire1_t br_taken;
  wire32_t pc_next;

  wire1_t dest_en, src1_en, src2_en;
  wire1_t src1_busy, src2_busy;
  wire4_t src1_latency, src2_latency; // 非单周期指令唤醒的latency 暂时不用
  wire1_t src1_is_pc;
  wire1_t src2_is_imm;
  wire3_t func3;
  wire1_t func7_5;
  wire32_t imm; // 好像不用32bit 先用着
  wire32_t pc;  // 未来将会优化pc的获取
  tag_t tag;
  wire12_t csr_idx;
  rob_t rob_idx;
  stq_t stq_idx;
  wire32_t pre_sta_mask;
  wire32_t pre_std_mask;

  // ROB 信息
  wire2_t uop_num;
  wire2_t cplt_num;
  wire1_t rob_flag; // 用于对比指令年龄

  // 异常信息
  wire1_t page_fault_inst;
  wire1_t page_fault_load;
  wire1_t page_fault_store;
  wire1_t illegal_inst;

  InstType type;
  UopType op;
  wire4_t amoop;

  // for debug
  bool difftest_skip;
  int64_t inst_idx;
} Inst_uop;

typedef struct {
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
  uint64_t ret_br_num = 0;

  uint64_t cond_mispred_num = 0;
  uint64_t jalr_mispred_num = 0;
  uint64_t ret_mispred_num = 0;

  uint64_t jalr_dir_mispred = 0;
  uint64_t jalr_addr_mispred = 0;

  uint64_t cond_dir_mispred = 0;
  uint64_t cond_addr_mispred = 0;

  uint64_t ret_dir_mispred = 0;
  uint64_t ret_addr_mispred = 0;

  uint64_t rob_entry_stall = 0;
  uint64_t idu_br_stall = 0;
  uint64_t idu_tag_stall = 0;
  uint64_t ren_reg_stall = 0;

  uint64_t isu_entry_stall[IQ_NUM];
  uint64_t isu_raw_stall[IQ_NUM];
  uint64_t isu_ready_num[IQ_NUM];

  void perf_reset() {
    cycle = 0;
    commit_num = 0;
    // cache
    cache_access_num = 0;
    cache_miss_num = 0;

    // bpu
    cond_br_num = 0;
    jalr_br_num = 0;
    ret_br_num = 0;

    cond_mispred_num = 0;
    jalr_mispred_num = 0;
    ret_mispred_num = 0;

    jalr_dir_mispred = 0;
    jalr_addr_mispred = 0;

    cond_dir_mispred = 0;
    cond_addr_mispred = 0;

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
           1 - (cond_mispred_num + jalr_mispred_num + ret_mispred_num) /
                   (double)(cond_br_num + jalr_br_num + ret_br_num));

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

    printf("\033[1;32mret    accuracy : %f\033[0m\n",
           1 - (ret_mispred_num) / (double)(ret_br_num));
    printf("\033[1;32mnum        : %ld\033[0m\n", ret_br_num);
    printf("\033[1;32mmispred    : %ld\033[0m\n", ret_mispred_num);
    printf("\033[1;32maddr error : %ld\033[0m\n", ret_addr_mispred);
    printf("\033[1;32mdir  error : %ld\033[0m\n", ret_dir_mispred);
    printf("\n");
    printf("\033[1;32m*********STALL COUNTER************\033[0m\n");
    printf("\033[1;32mrob     stall : %ld\033[0m\n", rob_entry_stall);
    printf("\033[1;32midu br  stall : %ld\033[0m\n", idu_br_stall);
    printf("\033[1;32midu tag stall : %ld\033[0m\n", idu_tag_stall);
    printf("\033[1;32mren reg stall : %ld\033[0m\n", ren_reg_stall);
    printf("\n");
    printf("\033[1;32m*********ISU COUNTER************\033[0m\n");

    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miss     stall : %ld\033[0m\n", isu_entry_stall[i]);
    }
    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miq%d ready  num : %f\033[0m\n", i,
             isu_ready_num[i] / (double)cycle);
    }
    for (int i = 0; i < IQ_NUM; i++) {
      printf("\033[1;32miq%d raw  num : %ld\033[0m\n", i, isu_raw_stall[i]);
    }
  }
};

extern long long sim_time;

class SimContext {
public:
  Perf_count perf;
  bool sim_end = false;
  bool stall = false;
  bool misprediction = false;
  bool exception = false;
  uint32_t *p_memory;
};
