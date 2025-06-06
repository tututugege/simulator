#include "../front_IO.h"
#include "../front_module.h"
#include "../frontend.h"
#include "config.h"
#include <cstdint>
#include <cstdio>
// no actual icache, just a simple simulation

extern uint32_t *p_memory;
void icache_top(struct icache_in *in, struct icache_out *out) {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    return;
  }
  out->icache_read_ready = true;
  // when BPU sends a valid read request
  if (in->icache_read_valid) {
    // read instructions from pmem
    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t pmem_address = in->fetch_address + (i * 4);
      out->fetch_group[i] = p_memory[pmem_address >> 2];
      if (DEBUG_PRINT) {
        printf("[icache] pmem_address: %x\n", pmem_address);
        printf("[icache] instruction : %x\n", out->fetch_group[i]);
      }
    }
  }
}
