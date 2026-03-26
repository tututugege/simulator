#pragma once
#include "ref.h"
#include <cstdint>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

extern CPU_state dut_cpu;
extern uint32_t *p_memory;
class SimContext;

void init_difftest(int);
void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory);
void get_state(CPU_state &dut_state, uint8_t &privilege, uint32_t *dut_memory);
void difftest_step(bool);
void difftest_skip();
uint64_t difftest_get_oracle_timer();
void difftest_dump_memory_line(const char *tag, uint32_t addr);
