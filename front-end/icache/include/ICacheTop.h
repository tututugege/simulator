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

  virtual ~ICacheTop() {}
};

ICacheTop *get_icache_instance();

#endif
