#include "config.h"
#include <cstdint>
#include <iostream>

class to_cRAT {
public:
  bool we[INST_WAY];
  uint32_t waddr[INST_WAY];
  uint32_t wdata[INST_WAY];
  uint32_t raddr[3 * INST_WAY];

  bool in_valid[INST_WAY];
  bool restore;
  uint32_t restore_idx;

  bool out_valid[ISSUE_WAY];
  uint32_t out_idx[ISSUE_WAY];
};

class from_cRAT {
public:
  uint32_t rdata[3 * INST_WAY];
  uint32_t gp_idx[INST_WAY];
};

class cRAT {
public:
  /*cRAT(int rport_num, int wport_num, int depth, int width, bool CAM);*/
  to_cRAT to_rat;
  from_cRAT from_rat;

  void read();
  void write();

  int alloc_gp();

private:
  uint32_t data[PRF_NUM];
  bool CAM_valid[PRF_NUM];

  // checkpoint
  bool gp[CHECKPOINT_NUM][PRF_NUM];
  bool gp_valid[CHECKPOINT_NUM];
};
