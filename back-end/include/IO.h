#pragma once

#include "config.h"
#include "frontend.h"
#include "util.h"
#include <cstdint>

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LSU

typedef struct {
  op_t ld_st;
  mem_sz_t mem_sz;
  int dst_preg;
  bool sign;
  bool valid;
} Ren_Lsu;

typedef struct {
  int lsq_entry;
  bool valid;
  bool ready;
} Lsu_Ren;

typedef struct {
  bool valid;
  op_t op;
  uint32_t vtag;
  uint32_t index;
  uint32_t word;
  uint32_t offset;
  uint8_t wdata_b4_shf[4];
  int lsq_entry;
} Prf_Lsu;

typedef struct {
  bool ready;
} Lsu_Prf;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct {
  Inst_info inst[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
} Dec_Ren;

typedef struct {
  bool ready;
} Ren_Dec;

typedef struct {
  bool dec_fire[FETCH_WIDTH];
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
  bool ready[FETCH_WIDTH];
  uint32_t enq_idx;
} Rob_Ren;

typedef struct {
  Inst_info inst[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool dis_fire[FETCH_WIDTH];
} Ren_Rob;

typedef struct {
  Wake_info wake;
} Prf_Awake;

typedef struct {
  Inst_info inst[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool dis_fire[FETCH_WIDTH];
} Ren_Iss;

typedef struct {
  bool ready[FETCH_WIDTH];
  Wake_info wake[5];
} Iss_Ren;

typedef struct {
  bool rollback;
  bool exception;
  bool mret;
} Rob_Broadcast;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
} Iss_Prf;

typedef struct {
  bool ready[ISSUE_WAY];
} Prf_Iss;

typedef struct {
  Inst_entry iss_entry[ISSUE_WAY];
  bool ready[ISSUE_WAY];
} Prf_Exe;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
  bool ready[ISSUE_WAY];
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
