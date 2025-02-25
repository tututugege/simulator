#include "PTAB.h"
#include "config.h"

void PTAB::init() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    entry[i].valid = false;
  }
}

void PTAB::comb_alloc() {
  int j = 0;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i]) {

      for (; j < MAX_BR_NUM; j++) {
        if (entry[j].valid == false) {
          entry_1[j].br_pc = in.ptab_wdata[i];
          entry_1[j].valid = true;
          out.ready[i] = true;
          out.ptab_idx[i] = j;
        }
      }

      if (j == MAX_BR_NUM)
        out.ready[i] = false;
    } else {
      out.ready[i] = true;
    }
  }
}

void PTAB::comb_read() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    out.entry[i] = entry[in.ptab_idx[i]];
    entry_1[i].valid = false;
  }
}
