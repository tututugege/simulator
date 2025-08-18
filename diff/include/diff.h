#pragma once
/*#include <top_config.h>*/
#include <config.h>
#include <cstdint>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };
typedef struct CPU_state {
  uint32_t gpr[32];
  uint32_t csr[21];
  uint32_t pc;

  uint32_t store_addr;
  uint32_t store_data;
  bool store;
} CPU_state;

extern CPU_state dut_cpu;
extern uint32_t *p_memory;

void init_difftest(int);
void difftest_step();
void difftest_skip();
