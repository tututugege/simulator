#pragma once

#include "config.h"
#include <cstdint>
#include <sys/types.h>
#include <util.h>

typedef struct {
  reg32_t addr;
  reg32_t size;
  reg32_t data;

  reg1_t commit;
  reg1_t addr_valid;
  reg1_t data_valid;
  reg1_t valid;
  uint32_t tag;
} STQ_entry;

typedef struct {
  Inst_uop uop[FETCH_WIDTH]; // 3 2 2 2
  wire1_t valid[FETCH_WIDTH];
} Dec_Ren;

typedef struct {
  wire1_t ready;
} Ren_Dec;

typedef struct {
  wire1_t fire[FETCH_WIDTH];
  wire1_t ready;
} Dec_Front;

typedef struct {
  wire32_t inst[FETCH_WIDTH];
  wire32_t pc[FETCH_WIDTH];
  wire1_t valid[FETCH_WIDTH];
  wire1_t predict_dir[FETCH_WIDTH];

  wire1_t alt_pred[FETCH_WIDTH];
  wire8_t altpcpn[FETCH_WIDTH];
  wire8_t pcpn[FETCH_WIDTH]; // 这仨我也不知道是什么
  wire32_t predict_next_fetch_address[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
} Front_Dec;

typedef struct {
  wire1_t mispred;
  wire16_t br_mask;
  wire4_t br_tag;
  wire7_t redirect_rob_idx;
} Dec_Broadcast;

typedef struct {
  Inst_entry commit_entry[COMMIT_WIDTH];
} Rob_Commit;

typedef struct {
  wire1_t ready;
  wire1_t empty;
  wire1_t stall;
  wire7_t enq_idx;
  wire1_t rob_flag;
} Rob_Dis;

typedef struct {
  Inst_uop uop[FETCH_WIDTH];
  wire1_t valid[FETCH_WIDTH];
  wire1_t dis_fire[FETCH_WIDTH];
} Dis_Rob;

typedef struct {
  Inst_uop uop[FETCH_WIDTH];
  wire1_t valid[FETCH_WIDTH];
} Ren_Dis;

typedef struct {
  wire1_t ready;
} Dis_Ren;

typedef struct {
  Wake_info wake;
} Prf_Awake;

typedef struct {
  Inst_uop uop[IQ_NUM][2];
  wire1_t valid[IQ_NUM][2];
  wire1_t dis_fire[IQ_NUM][2];
} Dis_Iss;

// TODO: MAGIC NUMBER
typedef struct {
  wire1_t ready[IQ_NUM][2];
} Iss_Dis;

typedef struct {
  Wake_info wake[ALU_NUM];
} Iss_Awake;

typedef struct {
  wire1_t flush;
  wire1_t mret;
  wire1_t sret;
  wire1_t ecall;
  wire1_t exception;

  wire1_t page_fault_inst;
  wire1_t page_fault_load;
  wire1_t page_fault_store;
  wire1_t illegal_inst;
  wire1_t interrupt;
  wire32_t trap_val;
  wire32_t pc;
} Rob_Broadcast;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
} Iss_Prf;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
  wire1_t ready[ISSUE_WAY];
} Prf_Exe;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
} Exe_Prf;

typedef struct {
  wire1_t ready[ISSUE_WAY];
} Exe_Iss;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
} Prf_Rob;

typedef struct {
  wire1_t mispred;
  wire32_t redirect_pc;
  wire7_t redirect_rob_idx;
  wire4_t br_tag;
} Prf_Dec;

typedef struct {
  wire4_t tag[FETCH_WIDTH];
  wire1_t valid[FETCH_WIDTH];
  wire1_t dis_fire[FETCH_WIDTH];
} Dis_Stq;

typedef struct {
  wire1_t ready[2];
  wire4_t stq_idx;
} Stq_Dis;

typedef struct {
  // 地址写入
  Inst_entry addr_entry;
  // 数据写入
  Inst_entry data_entry;
} Exe_Stq;

typedef struct {
  wire1_t we;
  wire1_t re;
  wire12_t idx;
  wire32_t wdata;
  wire32_t wcmd;
} Exe_Csr;

typedef struct {
  wire32_t rdata;
} Csr_Exe;

typedef struct {
  wire1_t interrupt_req;
  wire32_t epc;
  wire32_t trap_pc;
} Csr_Rob;

typedef struct {
  wire1_t interrupt_resp;
  wire1_t commit;
} Rob_Csr;
