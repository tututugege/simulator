#pragma once

#include "config.h"
#include <sys/types.h>
#include <util.h>

typedef struct {
  reg32_t addr;
  reg32_t size;
  reg32_t data;
  reg32_t inst;

  reg1_t commit;
  reg1_t addr_valid;
  reg1_t data_valid;
  reg1_t valid;
  reg4_t tag;
  reg2_t issued;
} STQ_entry;

typedef struct {
  Inst_uop uop[FETCH_WIDTH];
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
  wire8_t pcpn[FETCH_WIDTH];
  wire32_t predict_next_fetch_address[FETCH_WIDTH];
  wire32_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
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

// TODO: MAGIC NUMBER 2
typedef struct {
  Inst_uop uop[IQ_NUM][2];
  wire1_t valid[IQ_NUM][2];
  wire1_t dis_fire[IQ_NUM][2];
} Dis_Iss;

// TODO: MAGIC NUMBER 2
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
  wire1_t fence;

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
  wire4_t tag[2];
  wire1_t valid[2];
  wire1_t dis_fire[2];
} Dis_Stq;

// TODO: MAGIC NUMBER 2
typedef struct {
  wire1_t ready[2];
  wire4_t stq_idx;
} Stq_Dis;

typedef struct {
  wire1_t fence_stall;
} Stq_Front;

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
} Csr_Rob;

typedef struct {
  wire32_t epc;
  wire32_t trap_pc;
} Csr_Front;

typedef struct {
  wire32_t sstatus;
  wire32_t mstatus;
  wire32_t satp;
  wire2_t privilege;
} Csr_Status;

typedef struct {
  wire1_t interrupt_resp;
  wire1_t commit;
} Rob_Csr;

typedef struct {
  wire1_t en;
  wire1_t wen;
  wire32_t addr;
  wire32_t wdata;
  wire8_t wstrb;

  Inst_uop uop;
} Mem_REQ;
typedef struct {
  wire1_t ready;
} Mem_READY;
typedef struct {
  wire1_t wen;
  wire1_t valid;
  wire32_t data;

  wire32_t addr;
  Inst_uop uop;
} Mem_RESP;

typedef struct {
  wire1_t flush;
  wire1_t mispred;
  wire16_t br_mask;
} Dcache_CONTROL;

typedef struct {
  wire1_t valid;
  wire1_t wen;
  wire32_t addr;
  wire32_t wstrb;
  wire32_t wdata;

  Inst_uop uop;
} Dcache_MSHR;

typedef struct {
  wire1_t stall_ld;
  wire1_t stall_st;
} WB_Arbiter_Dcache;

typedef struct {
  wire1_t valid;
  wire32_t wdata;

  wire1_t flush;
  wire1_t mispred;
  wire16_t br_mask;

  Inst_uop uop;
} Dcache_WriteBuffer;

typedef struct {
  wire1_t stall;
} WriteBuffer_Dcache;

typedef struct {
  wire1_t en;
  wire1_t wen;
  wire8_t sel;
  wire8_t len;
  wire1_t done;
  wire1_t last;
  wire2_t size;
  wire32_t addr;
  wire32_t wdata;
} EXMem_CONTROL;
typedef struct {
  wire32_t data;
  wire1_t last;
  wire1_t done;
} EXMem_DATA;
typedef struct {
  EXMem_CONTROL control;
  EXMem_DATA data;
} EXMem_IO;

typedef struct {
  wire1_t valid;
  wire32_t addr;
  wire32_t data[4];
} MSHR_WB;

typedef struct {
  wire1_t ready;
} WB_MSHR;
typedef struct {
  wire1_t arbiter_priority;
} WB_Arbiter;

typedef struct {
  wire1_t valid;
  wire32_t addr;
  wire32_t rdata;

} MSHR_FWD;

typedef struct {
  bool prority;
} MSHR_Arbiter;

typedef struct {
  bool stall;
} Prf_Dcache;
