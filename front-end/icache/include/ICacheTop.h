#ifndef ICACHE_TOP_H
#define ICACHE_TOP_H

// ICacheTop is glue logic only:
// - it binds MMU/PTW/AXI runtime objects
// - it folds external table responses into the icache_module interface
// - it translates external MMU/PTW/AXI views into explicit glue IO
//
// Do not add front-end timing/register state here. Request/miss/refill state
// must stay inside icache_module or dedicated external adapters. Top-level
// perf/accounting should also stay outside this glue layer.

#include "../../front_IO.h"
#include <cstdint>

class SimContext;
class PtwMemPort;
class PtwWalkPort;
struct ICacheMemPortReq;
struct ICacheMemPortResp;
namespace axi_interconnect {
struct ReadMasterPort_t;
}

class ICacheTop {
protected:
  struct icache_in *in = nullptr;
  struct icache_out *out = nullptr;
  SimContext *ctx = nullptr;

public:
  void setIO(struct icache_in *in_ptr, struct icache_out *out_ptr) {
    in = in_ptr;
    out = out_ptr;
  }

  void setContext(SimContext *c) { ctx = c; }

  virtual void comb() = 0;
  virtual void seq() = 0;
  virtual void peek_ready() = 0;
  virtual void dump_debug_state() const = 0;
  virtual void set_ptw_mem_port(PtwMemPort *port) { (void)port; }
  virtual void set_ptw_walk_port(PtwWalkPort *port) { (void)port; }
  virtual void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) {
    (void)port;
  }
  virtual void set_mem_probe_ports(ICacheMemPortReq *req_port,
                                   ICacheMemPortResp *resp_port) {
    (void)req_port;
    (void)resp_port;
  }

  virtual ~ICacheTop() {}
};

ICacheTop *get_icache_instance();

namespace icache_top_n {

inline constexpr int ICACHE_TOP_TRUE_GLUE_LINE_WORDS = ICACHE_LINE_SIZE / 4;

// Packed top-glue combinational boundary for the true ICacheTop path.
// PI contains top-level control and icache_module visible state/output.
// PO is the functional icache_out payload driven by the true ICacheTop wrapper.
// perf/debug observability fields are intentionally excluded from this training
// IO boundary and remain normal simulator-side statistics wiring.
inline constexpr int ICACHE_TOP_TRUE_GLUE_PI_WIDTH =
    1 + // reset
    1 + // recovery hold (itlb_flush/fence_i/invalidate_req)
    1 + // registered module ready
    1 + // registered lookup pending
    1 + // registered request valid
    1 + // registered miss txid valid
    3 + // module state
    1 + // module AXI state
    1 + // module ifu_req_ready
    1 + // module ifu_resp_valid
    32 + // module response PC
    ICACHE_TOP_TRUE_GLUE_LINE_WORDS * 32 + // module response cache line
    1;  // module response page fault

inline constexpr int ICACHE_TOP_TRUE_GLUE_PO_WIDTH =
    4 +  // icache ready/complete for the two frontend lanes
    FETCH_WIDTH * 32 + // fetch_group
    FETCH_WIDTH +      // page_fault_inst
    FETCH_WIDTH +      // inst_valid
    FETCH_WIDTH * 32 + // fetch_group_2
    FETCH_WIDTH +      // page_fault_inst_2
    FETCH_WIDTH +      // inst_valid_2
    32 +               // fetch_pc
    32;                // fetch_pc_2

void icache_top_true_glue_io_generator(const bool *pi, bool *po);

} // namespace icache_top_n

#endif
