#include "ras.h"
#include "../../frontend.h"
#include <cassert>

#define RAS_ENTRY_NUM 64
#define RAS_CNT_LEN 8 // cnt for repeated call

uint32_t ras[RAS_ENTRY_NUM];
uint32_t ras_cnt[RAS_ENTRY_NUM];
uint32_t ras_sp;

void ras_push(uint32_t addr) {
  DEBUG_LOG_SMALL_2("[ras_push] pushing pc: %x\n", addr);
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
    DEBUG_LOG_SMALL_2("[ras_pop] popping pc: %x\n", ras[ras_sp]);
    return ras[ras_sp];
  } else if (ras_cnt[ras_sp] == 1) {
    ras_cnt[ras_sp] = 0;
    ras_sp = (ras_sp + RAS_ENTRY_NUM - 1) % RAS_ENTRY_NUM;
    DEBUG_LOG_SMALL_2("[ras_pop] popping pc: %x\n", ras[ras_sp + 1]);
    return ras[ras_sp + 1];
  } else
    return -1; // null on top
}
