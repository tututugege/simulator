#include "FrontTop.h"
#include "config.h"
#include "front_module.h"
#include "oracle.h"
#include "util.h"
#include <cstring>

namespace {
inline void sync_icache_ptw_ports(const FrontTop &front) {
  icache_ptw_mem_port = front.icache_ptw_mem_port;
  icache_ptw_walk_port = front.icache_ptw_walk_port;
  icache_mem_read_port = front.icache_mem_read_port;
}
} // namespace

void FrontTop::init() {
  CsrStatusIO *bound_csr = in.csr_status;
  sync_icache_ptw_ports(*this);
  icache_set_context(ctx);
  std::memset(&in, 0, sizeof(in));
  std::memset(&out, 0, sizeof(out));
  in.csr_status = bound_csr;
  Assert(ctx != nullptr && "FrontTop::init requires bound SimContext");
  Assert(in.csr_status != nullptr && "FrontTop::init requires bound csr_status");
  in.FIFO_read_enable = true;
#ifdef CONFIG_BPU
  in.reset = true;
  step_bpu();
  in.reset = false;
#endif
}

void FrontTop::step_bpu() {
  sync_icache_ptw_ports(*this);
  front_top(&in, &out);
}

void FrontTop::step_oracle() {
  sync_icache_ptw_ports(*this);
  get_oracle(in, out);
}
