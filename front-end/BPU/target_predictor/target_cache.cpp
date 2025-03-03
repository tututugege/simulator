#include "target_cache.h"

#define TC_ENTRY_NUM 2048
#define BHT_ENTRY_NUM 2048
#define BHT_LEN 32

static uint32_t bht[BHT_ENTRY_NUM];
static uint32_t target_cache[TC_ENTRY_NUM];

uint32_t tc_pred(uint32_t pc) {
  uint32_t bht_idx = pc % BHT_ENTRY_NUM;
  uint32_t tc_idx = (bht[bht_idx] ^ pc) % TC_ENTRY_NUM;
  return target_cache[tc_idx];
}

void bht_update(uint32_t pc, bool pc_dir) {
  uint32_t bht_idx = pc % BHT_ENTRY_NUM;
  bht[bht_idx] = (bht[bht_idx] << 1) | pc_dir;
}

void tc_update(uint32_t pc, uint32_t actualAddr) {
  uint32_t bht_idx = pc % BHT_ENTRY_NUM;
  uint32_t tc_idx = (bht[bht_idx] ^ pc) % TC_ENTRY_NUM;
  target_cache[tc_idx] = actualAddr;
}