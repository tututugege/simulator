#include "ras.h"

#define RAS_ENTRY_NUM 64
#define RAS_CNT_LEN 8 // cnt for repeated call

static uint32_t ras[RAS_ENTRY_NUM];
static uint32_t ras_cnt[RAS_ENTRY_NUM];
static uint32_t ras_sp;

void ras_push(uint32_t addr) {
  if (addr == ras[ras_sp]) {
    ras_cnt[ras_sp]++;
    return;
  }
  ras_sp = (ras_sp + 1) % RAS_ENTRY_NUM;
  ras[ras_sp] = addr;
  ras_cnt[ras_sp] = 1;
}

uint32_t ras_pop() {
  if (ras_cnt[ras_sp] > 1) {
    ras_cnt[ras_sp]--;
    return ras[ras_sp];
  } else if (ras_cnt[ras_sp] == 1) {
    ras_cnt[ras_sp] = 0;
    ras_sp = (ras_sp + RAS_ENTRY_NUM - 1) % RAS_ENTRY_NUM;
    return ras[ras_sp + 1];
  } else
    return -1; // null on top
}