#pragma once
#include "FIFO.h"
#include "cRAT.h"
#include "config.h"
#include <cstdint>
typedef struct Rename_out {
  int src1_preg_idx[INST_WAY];
  int src2_preg_idx[INST_WAY];
  int dest_preg_idx[INST_WAY];
  int old_dest_preg_idx[INST_WAY];
  int gp_idx[INST_WAY];

  bool src1_raw[INST_WAY];
  bool src2_raw[INST_WAY];
  bool valid[INST_WAY]; // to dispatch
  bool ready[INST_WAY]; // to_decode
  bool all_ready;
} Rename_out;

typedef struct Rename_in {
  // rename
  bool valid[INST_WAY]; // from decode
  bool ready[INST_WAY]; // from dispatch
  bool all_ready;
  int src1_areg_idx[INST_WAY];
  int src1_areg_en[INST_WAY];
  int src2_areg_idx[INST_WAY];
  int src2_areg_en[INST_WAY];
  int dest_areg_idx[INST_WAY];
  int dest_areg_en[INST_WAY];

  // commit 更新arch RAT
  bool commit_valid[ISSUE_WAY];
  bool commit_gp_idx[ISSUE_WAY];
  int commit_dest_en[ISSUE_WAY];
  int commit_dest_preg_idx[ISSUE_WAY];
  int commit_dest_areg_idx[ISSUE_WAY];
  int commit_old_dest_preg_idx[ISSUE_WAY];

  // branch 使用checkpoint恢复并回收
  bool br_taken;
  int br_gp_idx;
} Rename_in;

class Rename {
public:
  Rename_in in;
  Rename_out out;
  void init();
  void seq();  // 时序逻辑
  void comb(); // 组合逻辑

  // debug
  void print_reg();
  void print_RAT();
  uint32_t reg(int idx);
  int arch_RAT[ARF_NUM];

private:
  int cRAT_read(int areg_idx);
  void cRAT_write();

  int alloc_reg();
  void free_reg(int idx);

  void free_gp(int idx);
  void gp_write(int idx);

  cRAT RAT;
  FIFO<uint32_t> free_list =
      FIFO<uint32_t>(INST_WAY, ISSUE_WAY, PRF_NUM - ARF_NUM, 6);

  // register
  bool gp_v[CHECKPOINT_NUM];
  bool gp[CHECKPOINT_NUM][PRF_NUM]; // CAM

  // register next val
  bool gp_v_1[CHECKPOINT_NUM];
  bool gp_1[CHECKPOINT_NUM][PRF_NUM]; // CAM
};
