#include "PTAB.h"
void PTAB::init() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    entry[i].valid = false;
  }
}
void PTAB::read() {
  for (int i = 0; i < ALU_NUM; i++) {
    out.entry[i] = entry[in.br_idx[i]];
  }
}
