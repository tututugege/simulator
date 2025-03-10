#pragma once

#include "config.h"
#include "frontend.h"
#include <cstdint>
#include <sys/types.h>

typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t data;

  bool compelete;
  bool valid;
  uint32_t tag;
} STQ_entry;

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
  bool empty;
  bool stall;
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

// TODO: MAGIC NUMBER
typedef struct {
  bool ready[FETCH_WIDTH];
  Wake_info wake[6];
} Iss_Ren;

typedef struct {
  bool rollback;
  bool mret;
  bool exception;
  uint32_t pc;
  uint32_t cause;
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
  uint32_t tag[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool dis_fire[FETCH_WIDTH];
} Ren_Stq;

typedef struct {
  bool ready[FETCH_WIDTH];
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
