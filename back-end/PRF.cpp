#include "config.h"
#include <PRF.h>

void PRF::read(int preg_idx[PRF_RD_NUM], int rdata[PRF_RD_NUM]) {

  for (int i = 0; i < PRF_RD_NUM; i++) {
    rdata[i] = PRF[preg_idx[i]];
  }
}

void PRF::write() {
  for (int i = 0; i < PRF_WR_NUM; i++) {
    if (wen[i]) {
      PRF[wr_idx[i]] = wdata[i];
    }
  }
}
