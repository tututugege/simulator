#pragma once

#include "config.h"
#include <cstdint>

typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t data;

  bool compelete;
  bool valid;
  uint32_t tag;
} STQ_entry;

typedef struct {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
} Dec_Ren;

typedef struct {
  bool ready;
} Ren_Dec;

typedef struct {
  bool dec_fire[INST_WAY];
  bool ready;
} Dec_Front;

typedef struct {
  uint32_t inst[INST_WAY];
  uint32_t pc[INST_WAY];
  bool valid[INST_WAY];
  bool predict_dir[INST_WAY];
  bool alt_pred[INST_WAY];
  uint8_t altpcpn[INST_WAY];
  uint8_t pcpn[INST_WAY];
  uint32_t predict_next_fetch_address;

} Front_Dec;

typedef struct {
  bool mispred;
  uint32_t br_mask;
  uint32_t br_tag;
} Dec_Broadcast;

typedef struct {
  Inst_entry commit_entry[ISSUE_WAY];
} Rob_Commit;

typedef struct {
  bool ready[INST_WAY];
  uint32_t enq_idx;
} Rob_Ren;

typedef struct {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];
} Ren_Rob;

typedef struct {
  Wake_info wake[ISSUE_WAY];
} Prf_Awake;

typedef struct {
  Inst_info inst[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];
} Ren_Iss;

typedef struct {
  bool ready[INST_WAY];
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

typedef struct {
  uint32_t tag[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];
} Ren_Stq;

typedef struct {
  bool ready[INST_WAY];
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
