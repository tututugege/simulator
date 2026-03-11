#include "../front_IO.h"
#include "PtwMemPort.h"
#include "include/ICacheTop.h"
#include "include/icache_module.h"
#include <cassert>

// Define global ICache instance
icache_module_n::ICache icache;
PtwMemPort *icache_ptw_mem_port = nullptr;
PtwWalkPort *icache_ptw_walk_port = nullptr;
axi_interconnect::ReadMasterPort_t *icache_mem_read_port = nullptr;
static SimContext *icache_ctx = nullptr;

namespace {
void bind_icache_runtime(ICacheTop *instance) {
  static PtwMemPort *bound_mem_port = nullptr;
  static PtwWalkPort *bound_walk_port = nullptr;
  static axi_interconnect::ReadMasterPort_t *bound_read_port = nullptr;
  static SimContext *bound_ctx = nullptr;

  if (bound_mem_port != icache_ptw_mem_port) {
    instance->set_ptw_mem_port(icache_ptw_mem_port);
    bound_mem_port = icache_ptw_mem_port;
  }
  if (bound_walk_port != icache_ptw_walk_port) {
    instance->set_ptw_walk_port(icache_ptw_walk_port);
    bound_walk_port = icache_ptw_walk_port;
  }
  if (bound_read_port != icache_mem_read_port) {
    instance->set_mem_read_port(icache_mem_read_port);
    bound_read_port = icache_mem_read_port;
  }
  if (bound_ctx != icache_ctx) {
    instance->setContext(icache_ctx);
    bound_ctx = icache_ctx;
  }
}
} // namespace

void icache_seq_read(struct icache_in *in, struct icache_out *out) {
  assert(in != nullptr);
  assert(out != nullptr);
  (void)in;
  (void)out;
}

void icache_comb_calc(struct icache_in *in, struct icache_out *out) {
  assert(in != nullptr);
  assert(out != nullptr);
  if (!in->reset) {
    assert(in->csr_status != nullptr &&
           "icache_comb_calc requires csr_status when not in reset");
  }
  ICacheTop *instance = get_icache_instance();
  bind_icache_runtime(instance);
  instance->setIO(in, out);
  instance->step();
}

void icache_seq_write() {}

void icache_top(struct icache_in *in, struct icache_out *out) {
  icache_seq_read(in, out);
  icache_comb_calc(in, out);
  icache_seq_write();
}

void icache_set_context(SimContext *ctx) {
  icache_ctx = ctx;
}

void icache_set_ptw_mem_port(PtwMemPort *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_ptw_mem_port(port);
}

void icache_set_ptw_walk_port(PtwWalkPort *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_ptw_walk_port(port);
}

void icache_set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_mem_read_port(port);
}
