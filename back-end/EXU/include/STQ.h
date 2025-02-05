#pragma once
#include "IO.h"
#include <config.h>
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
  uint32_t tag[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];
} REN_STQ;

typedef struct {
  bool ready[INST_WAY];
  bool stq_valid[STQ_NUM];
  uint32_t stq_idx[INST_WAY];
} STQ_REN;

typedef struct {
  // 实际写入
  STQ_entry write;
  bool wr_valid;
  uint32_t wr_idx;
} EXU_STQ;

typedef struct {
  bool valid[INST_WAY];
  uint32_t tag[INST_WAY];
} STQ_ISU;

typedef struct {
  REN_STQ *ren2stq;
  EXU_STQ *exe2stq;
  STQ_ISU *stq2isu;
  STQ_REN *stq2ren;
  Rob_Commit *rob_commit;
} STQ_IO;

typedef struct {
  bool ready[INST_WAY];

  // 内存写端口
  bool wen;
  uint32_t wdata;
  uint32_t waddr;
  uint32_t wstrb;

  // 唤醒
  uint32_t st_idx;

  // 当前有效信息
  bool entry_valid[STQ_NUM];
} STQ_out;

class STQ {
public:
  STQ_IO io;
  void comb();
  void seq();
  void init();
  int enq_ptr;
  int deq_ptr;

private:
  STQ_entry entry[STQ_NUM];
  int commit_ptr;
  int count;
};
