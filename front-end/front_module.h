#ifndef FRONT_MODULE_H
#define FRONT_MODULE_H

#include "front_IO.h"

void BPU_top(struct BPU_in *in, struct BPU_out *out);
void BPU_change_pc_reg(uint32_t new_pc);

void icache_top(struct icache_in *in, struct icache_out *out);

void instruction_FIFO_top(struct instruction_FIFO_in *in,
                          struct instruction_FIFO_out *out);

void PTAB_top(struct PTAB_in *in, struct PTAB_out *out);

void front_top(struct front_top_in *in, struct front_top_out *out);

void front2back_FIFO_top(struct front2back_FIFO_in *in, struct front2back_FIFO_out *out);

#endif // FRONT_MODULE_H