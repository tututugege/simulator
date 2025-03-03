#ifndef TARGET_CACHE_H
#define TARGET_CACHE_H

#include <cstdint>

uint32_t tc_pred(uint32_t pc);
void tc_update(uint32_t pc, uint32_t actualAddr);
void bht_update(uint32_t pc, bool pc_dir);

#endif // TARGET_CACHE_H