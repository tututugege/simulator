#ifndef FRONT_MODULE_H
#define FRONT_MODULE_H

#include "front_IO.h"
#include <cstdint>

class PtwMemPort;
class PtwWalkPort;
class SimContext;
namespace axi_interconnect {
struct ReadMasterPort_t;
}
extern PtwMemPort *icache_ptw_mem_port;
extern PtwWalkPort *icache_ptw_walk_port;
extern axi_interconnect::ReadMasterPort_t *icache_mem_read_port;

void BPU_top(struct BPU_in *in, struct BPU_out *out);
void BPU_change_pc_reg(uint32_t new_pc);

void icache_top(struct icache_in *in, struct icache_out *out);
void icache_seq_read(struct icache_in *in, struct icache_out *out);
void icache_comb_calc(struct icache_in *in, struct icache_out *out);
void icache_seq_write();
void icache_set_context(SimContext *ctx);
void icache_set_ptw_mem_port(PtwMemPort *port);
void icache_set_ptw_walk_port(PtwWalkPort *port);
void icache_set_mem_read_port(axi_interconnect::ReadMasterPort_t *port);

void instruction_FIFO_top(struct instruction_FIFO_in *in,
                          struct instruction_FIFO_out *out);
void instruction_FIFO_seq_read(struct instruction_FIFO_in *in,
                               struct instruction_FIFO_out *out);
void instruction_FIFO_comb_calc(struct instruction_FIFO_in *in,
                                struct instruction_FIFO_out *out);
void instruction_FIFO_seq_write();

void PTAB_top(struct PTAB_in *in, struct PTAB_out *out);
void PTAB_seq_read(struct PTAB_in *in, struct PTAB_out *out);
void PTAB_comb_calc(struct PTAB_in *in, struct PTAB_out *out);
void PTAB_seq_write();

void front_top(struct front_top_in *in, struct front_top_out *out);

void front2back_FIFO_top(struct front2back_FIFO_in *in, struct front2back_FIFO_out *out);
void front2back_FIFO_seq_read(struct front2back_FIFO_in *in,
                              struct front2back_FIFO_out *out);
void front2back_FIFO_comb_calc(struct front2back_FIFO_in *in,
                               struct front2back_FIFO_out *out);
void front2back_FIFO_seq_write();


void fetch_address_FIFO_top(struct fetch_address_FIFO_in *in,
                            struct fetch_address_FIFO_out *out);
void fetch_address_FIFO_seq_read(struct fetch_address_FIFO_in *in,
                                 struct fetch_address_FIFO_out *out);
void fetch_address_FIFO_comb_calc(struct fetch_address_FIFO_in *in,
                                  struct fetch_address_FIFO_out *out);
void fetch_address_FIFO_seq_write();


bool ptab_peek_mini_flush();


#endif // FRONT_MODULE_H
