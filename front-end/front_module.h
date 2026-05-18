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


struct fetch_address_FIFO_read_data {
  fetch_addr_fifo_size_t size;
  wire1_t head_valid;
  fetch_addr_t head_entry;
};

struct instruction_FIFO_entry {
  inst_word_t instructions[FETCH_WIDTH];
  pc_t pc[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  predecode_type_t predecode_type[FETCH_WIDTH];
  target_addr_t predecode_target_address[FETCH_WIDTH];
  pc_t seq_next_pc;
};

struct instruction_FIFO_read_data {
  instruction_fifo_size_t size;
  wire1_t head_valid;
  instruction_FIFO_entry head_entry;
};

struct PTAB_entry {
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  pc_t predict_base_pc[FETCH_WIDTH];
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][TN_MAX];
  tage_tag_t tage_tag[FETCH_WIDTH][TN_MAX];
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
  wire1_t need_mini_flush;
  wire1_t dummy_entry;
};

struct PTAB_read_data {
  ptab_size_t size;
  wire1_t head_valid;
  PTAB_entry head_entry;
};

struct front2back_FIFO_entry {
  inst_word_t fetch_group[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  wire1_t predict_dir_corrected[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address_corrected;
  pc_t predict_base_pc[FETCH_WIDTH];
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][TN_MAX];
  tage_tag_t tage_tag[FETCH_WIDTH][TN_MAX];
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
};

struct front2back_FIFO_read_data {
  front2back_fifo_size_t size;
  wire1_t head_valid;
  front2back_FIFO_entry head_entry;
};

struct FetchAddrCombIn;
struct FetchAddrCombOut;
struct InstructionCombIn;
struct InstructionCombOut;
struct PtabCombIn;
struct PtabCombOut;
struct Front2BackCombIn;
struct Front2BackCombOut;

void BPU_top(struct BPU_in *in, struct BPU_out *out);
void icache_top(struct icache_in *in, struct icache_out *out);
void icache_seq_read(struct icache_in *in, struct icache_out *out);
void icache_peek_ready(struct icache_in *in, struct icache_out *out);
void icache_comb_calc(struct icache_in *in, struct icache_out *out);
void icache_seq_write();
void icache_dump_debug_state();
void icache_set_context(SimContext *ctx);
void front_set_context(SimContext *ctx);
void icache_set_ptw_mem_port(PtwMemPort *port);
void icache_set_ptw_walk_port(PtwWalkPort *port);
void icache_set_mem_read_port(axi_interconnect::ReadMasterPort_t *port);

void instruction_FIFO_seq_read(struct instruction_FIFO_in *in,
                               struct instruction_FIFO_read_data *rd);
void instruction_FIFO_comb(const InstructionCombIn &input,
                           InstructionCombOut &output);
void instruction_FIFO_comb_calc(struct instruction_FIFO_in *in,
                                const struct instruction_FIFO_read_data *rd,
                                struct instruction_FIFO_out *out,
                                struct instruction_FIFO_read_data *next_rd,
                                InstructionCombOut *step_req);
void instruction_FIFO_seq_write(const InstructionCombOut *req);

void PTAB_seq_read(struct PTAB_in *in, struct PTAB_read_data *rd);
void PTAB_comb(const PtabCombIn &input, PtabCombOut &output);
void PTAB_comb_calc(struct PTAB_in *in, const struct PTAB_read_data *rd,
                    struct PTAB_out *out, struct PTAB_read_data *next_rd,
                    PtabCombOut *step_req);
void PTAB_seq_write(const PtabCombOut *req);

void front_top(struct front_top_in *in, struct front_top_out *out);
void front_dump_debug_state();

void front2back_FIFO_seq_read(struct front2back_FIFO_in *in,
                              struct front2back_FIFO_read_data *rd);
void front2back_FIFO_comb(const Front2BackCombIn &input,
                          Front2BackCombOut &output);
void front2back_FIFO_comb_calc(struct front2back_FIFO_in *in,
                               const struct front2back_FIFO_read_data *rd,
                               struct front2back_FIFO_out *out,
                               struct front2back_FIFO_read_data *next_rd,
                               Front2BackCombOut *step_req);
void front2back_FIFO_seq_write(const Front2BackCombOut *req);

void fetch_address_FIFO_seq_read(struct fetch_address_FIFO_in *in,
                                 struct fetch_address_FIFO_read_data *rd);
void fetch_address_FIFO_comb(const FetchAddrCombIn &input,
                             FetchAddrCombOut &output);
void fetch_address_FIFO_comb_calc(struct fetch_address_FIFO_in *in,
                                  const struct fetch_address_FIFO_read_data *rd,
                                  struct fetch_address_FIFO_out *out,
                                  struct fetch_address_FIFO_read_data *next_rd,
                                  FetchAddrCombOut *step_req);
void fetch_address_FIFO_seq_write(const FetchAddrCombOut *req);

#endif // FRONT_MODULE_H
