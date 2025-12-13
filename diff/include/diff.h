#pragma once
/*#include <top_config.h>*/
#include <config.h>
#include <cstdint>
#include <ref.h>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

extern CPU_state dut_cpu;
extern uint32_t *p_memory;

void init_difftest(int);
void difftest_step(bool);
void difftest_skip();
