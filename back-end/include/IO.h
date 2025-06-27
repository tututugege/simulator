#pragma once

#include "config.h"
#include "frontend.h"
#include <cstdint>
#include <sys/types.h>

typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t data;

  bool complete;
  bool valid;
  bool amo_data1_valid;
  bool amo_data2_valid;
  uint32_t tag;
} STQ_entry;

typedef struct {
  Inst_uop uop[DECODE_WIDTH];
  bool valid[DECODE_WIDTH];
} Dec_Ren;

typedef struct {
  bool ready;
} Ren_Dec;

typedef struct {
  bool fire[FETCH_WIDTH];
  bool ready;
} Dec_Front;

typedef struct {
  uint32_t inst[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  uint32_t predict_next_fetch_address[FETCH_WIDTH];

  bool page_fault_inst[FETCH_WIDTH];
} Front_Dec;

typedef struct {
  bool mispred;
  uint32_t br_mask;
  uint32_t br_tag;
} Dec_Broadcast;

typedef struct {
  Inst_entry commit_entry[COMMIT_WIDTH];
} Rob_Commit;

typedef struct {
  bool ready[DECODE_WIDTH];
  bool empty;
  bool stall;
  uint32_t enq_idx;
} Rob_Ren;

typedef struct {
  Inst_uop uop[DECODE_WIDTH];
  bool valid[DECODE_WIDTH];
  bool dis_fire[DECODE_WIDTH];
} Ren_Rob;

typedef struct {
  Wake_info wake;
} Prf_Awake;

typedef struct {
  bool valid;
  AMO_op amoop;
  uint32_t load_data;
  uint32_t stq_idx;
} Prf_Stq;

typedef struct {
  Inst_uop uop[DECODE_WIDTH];
  bool valid[DECODE_WIDTH];
  bool dis_fire[DECODE_WIDTH];
} Ren_Iss;

// TODO: MAGIC NUMBER
typedef struct {
  bool ready[DECODE_WIDTH];
  Wake_info wake[ALU_NUM];
} Iss_Ren;

typedef struct {
  bool flush;
  bool mret;
  bool sret;
  bool ecall;
  bool exception;

  bool page_fault_inst;
  bool page_fault_load;
  bool page_fault_store;
  bool illegal_inst;
  uint32_t trap_val;

  uint32_t pc;
} Rob_Broadcast;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
} Iss_Prf;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
  bool ready[ISSUE_WAY];
} Prf_Exe;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
} Exe_Prf;

typedef struct {
  bool ready[ISSUE_WAY];
} Exe_Iss;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
} Prf_Rob;

typedef struct {
  bool mispred;
  uint32_t redirect_pc;
  uint32_t br_tag;
} Prf_Dec;

typedef struct {
  uint32_t tag[DECODE_WIDTH];
  bool valid[DECODE_WIDTH];
  bool dis_fire[DECODE_WIDTH];
} Ren_Stq;

typedef struct {
  bool ready[DECODE_WIDTH];
  bool stq_valid[STQ_NUM];
  uint32_t stq_idx;
} Stq_Ren;

typedef struct {
  // 实际写入
  Inst_entry entry;
} Exe_Stq;

typedef struct {
  bool valid[STQ_NUM];
} Stq_Iss;
