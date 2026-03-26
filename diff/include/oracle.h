#pragma once
#include "ref.h"
#include <cstdint>
void get_oracle(struct front_top_in &in, struct front_top_out &out);
void init_oracle(int img_size);
void init_oracle_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory,
                      uint8_t privilege);
uint64_t get_oracle_timer();
