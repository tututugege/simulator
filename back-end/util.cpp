#include "config.h"
#include <SRAM.h>
#include <cRAT.h>
#include <string.h>

int cRAT::alloc_gp() {
  for (int i = 0; i < CHECKPOINT_NUM; i++) {
    if (gp_valid[i] == false)
      return i;
  }

  assert(0);
  return -1;
}

void cRAT::read() {
  for (int i = 0; i < INST_WAY * 3; i++) {
    for (int j = 0; j < PRF_NUM; j++) {
      if (data[j] == to_rat.raddr[i] && CAM_valid[j]) {
        from_rat.rdata[i] = j;
        break;
      }
    }
  }

  // 分配checkpoint
  for (int i = 0; i < INST_WAY; i++) {
    if (to_rat.in_valid[i]) {
      from_rat.gp_idx[i] = alloc_gp();
    }
  }
}

void cRAT::write() {

  // 写入发射指令的checkpoint
  for (int i = 0; i < INST_WAY; i++) {

    if (to_rat.in_valid[i]) {
      for (int i = 0; i < PRF_NUM; i++) {
        gp[from_rat.gp_idx[i]][i] = CAM_valid[i];
      }

      if (to_rat.we[i]) {
        for (int j = 0; j < PRF_NUM; j++) {
          if (data[j] == to_rat.wdata[i] && gp_valid[j]) {
            gp[from_rat.gp_idx[i]][j] = false;
            break;
          }
        }
        gp[from_rat.gp_idx[i]][to_rat.waddr[i]] = false;
      }
    }
  }

  // 释放checkpoint
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (to_rat.out_valid[i]) {
      gp_valid[to_rat.out_idx[i]] = false;
    }
  }

  if (to_rat.restore) {
    // 恢复

  } else {
    // 写回
    for (int i = 0; i < INST_WAY; i++) {
      if (to_rat.we[i]) {
        for (int j = 0; j < PRF_NUM; j++) {
          if (data[j] == to_rat.wdata[i] && CAM_valid[j]) {
            CAM_valid[j] = false;
            break;
          }
        }

        data[to_rat.waddr[i]] = to_rat.wdata[i];
        CAM_valid[to_rat.waddr[i]] = true;
      }
    }
  }
}
