#ifndef ICACHE_TOP_H
#define ICACHE_TOP_H

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
  uint64_t access_delta = 0;
  uint64_t miss_delta = 0;

public:
  void setIO(struct icache_in *in_ptr, struct icache_out *out_ptr) {
    in = in_ptr;
    out = out_ptr;
  }

  void setContext(SimContext *c) { ctx = c; }

  virtual void comb() = 0;
  virtual void seq() = 0;
  virtual void peek_ready() = 0;
  virtual void set_ptw_mem_port(PtwMemPort *port) { (void)port; }
  virtual void set_ptw_walk_port(PtwWalkPort *port) { (void)port; }
  virtual void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) {
    (void)port;
  }

  void syncPerf();

  virtual void step() {
    comb();
    seq();
    syncPerf();
  }

  virtual ~ICacheTop() {}
};

ICacheTop *get_icache_instance();

#endif
