#pragma once

#include "config.h"
#include <cstdint>
#include <vector>

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
  bool dec_fire[INST_WAY];
  bool ready[INST_WAY];
} Ren_Dec;

typedef struct {
  bool dec_fire[INST_WAY];
} Dec_Front;

typedef struct {
  uint32_t inst[INST_WAY];
  uint32_t pc[INST_WAY];
  bool valid[INST_WAY];
} Front_Dec;

typedef struct {
  uint32_t br_mask;
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
  Wake_info wake[EXU_NUM];
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
  vector<vector<Inst_entry>> iss_pack;
} Iss_Prf;

typedef struct {
  vector<vector<bool>> ready;
  // store唤醒
  /*bool st_valid;*/
  /*uint32_t st_idx;*/
} Exe_Iss;

typedef struct {
  vector<vector<Inst_entry>> iss_pack;
  bool ready[EXU_NUM]; // write back
} Prf_Exe;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
  vector<vector<bool>> ready;
} Exe_Prf;

typedef struct {
  Inst_entry entry[ISSUE_WAY];
} Prf_Rob;

typedef struct {
  bool mispred;
  uint32_t br_tag;
} Exe_Broadcast;

typedef struct {
  uint32_t tag[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];
} Ren_Stq;

typedef struct {
  bool ready[INST_WAY];
  bool stq_valid[STQ_NUM];
  uint32_t stq_idx[INST_WAY];
} Stq_Ren;

typedef struct {
  // 实际写入
  Inst_entry entry;
} Exe_Stq;

typedef struct {
  bool valid[INST_WAY];
  uint32_t tag[INST_WAY];
} Stq_Iss;
