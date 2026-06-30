#include "FrontTop.h"
#include "DeadlockDebug.h"
#include "config.h"
#include "front_module.h"
#include "oracle.h"
#include "train_IO.h"
#include "util.h"
#include <cstring>
#include <memory>

namespace {
inline void sync_icache_ptw_ports(const FrontTop &front) {
  icache_ptw_mem_port = front.icache_ptw_mem_port;
  icache_ptw_walk_port = front.icache_ptw_walk_port;
  icache_mem_req_port = front.icache_mem_req_port;
  icache_mem_resp_port = front.icache_mem_resp_port;
  icache_mem_read_port = front.icache_mem_read_port;
}
} // namespace

FrontTop::FrontTop()
    : read_data_(std::make_unique<FrontReadData>()),
      update_req_(std::make_unique<FrontUpdateRequest>()) {}

FrontTop::~FrontTop() = default;

void FrontTop::init() {
  CsrStatusIO *bound_csr = in.csr_status;
  sync_icache_ptw_ports(*this);
  front_set_context(ctx);
  icache_set_context(ctx);
  deadlock_debug::register_front_dump_cb(front_dump_debug_state);
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

void FrontTop::seq_read_bpu() {
  sync_icache_ptw_ports(*this);
  front_top_seq_read(in, *read_data_);
}

void FrontTop::comb_bpu() {
  sync_icache_ptw_ports(*this);
  *update_req_ = {};
  front_comb_calc(in, *read_data_, out, *update_req_);
}

void FrontTop::comb_apply_front2back_accept(bool accept) {
  auto &front2back_req = update_req_->front2back_fifo_req;
  front2back_req.pop_en = accept && !front2back_req.clear_fifo;

  if (update_req_->front_state.valid) {
    int next_size =
        static_cast<int>(read_data_->front2back_fifo_rd_snapshot.size);
    if (front2back_req.clear_fifo) {
      next_size = 0;
    }
    if (front2back_req.push_en) {
      ++next_size;
    }
    if (front2back_req.pop_en) {
      --next_size;
    }
    if (next_size < 0) {
      next_size = 0;
    }
    if (next_size > FRONT2BACK_FIFO_SIZE) {
      next_size = FRONT2BACK_FIFO_SIZE;
    }
    update_req_->front_state.next_front2back_fifo_full =
        (next_size == FRONT2BACK_FIFO_SIZE);
    update_req_->front_state.next_front2back_fifo_empty =
        (next_size == 0);
  }
}

void FrontTop::seq_write_bpu() {
  sync_icache_ptw_ports(*this);
  front_top_seq_write(in, *update_req_, in.reset);
}

void FrontTop::step_bpu() {
  seq_read_bpu();
  comb_bpu();
  seq_write_bpu();
}

void FrontTop::step_oracle() {
  sync_icache_ptw_ports(*this);
  get_oracle(in, out);
}

void FrontTop::dump_debug_state() const { front_dump_debug_state(); }
