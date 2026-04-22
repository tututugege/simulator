#include "front_fifo.h"
#include "../train_IO.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void fetch_address_FIFO_comb(const FetchAddrCombIn &in, FetchAddrCombOut &out) {
  std::memset(&out, 0, sizeof(FetchAddrCombOut));

  out.out_regs.full = false;
  out.out_regs.empty = true;
  out.out_regs.read_valid = false;
  out.out_regs.fetch_address = 0;

  if (in.inp.reset) {
    out.clear_fifo = true;
    return;
  }

  bool has_data_before_read = (in.rd.size > 0);
  uint8_t queue_size_before = in.rd.size;
  if (in.inp.refetch) {
    out.clear_fifo = true;
    has_data_before_read = false;
    queue_size_before = 0;
  }

  const bool do_write = in.inp.write_enable;
  const bool do_read = in.inp.read_enable && (has_data_before_read || do_write);

  if (do_read) {
    if (has_data_before_read && !in.rd.head_valid) {
      std::printf("[fetch_address_FIFO] ERROR: missing head snapshot for same-cycle reread\n");
      std::exit(1);
    }
    out.out_regs.read_valid = true;
    out.out_regs.fetch_address =
        has_data_before_read ? in.rd.head_entry : in.inp.fetch_address;
  }

  out.push_en = do_write;
  out.push_data = in.inp.fetch_address;
  out.pop_en = do_read;

  int next_size = static_cast<int>(queue_size_before);
  next_size += out.push_en ? 1 : 0;
  next_size -= out.pop_en ? 1 : 0;
  if (next_size < 0) {
    next_size = 0;
  }
  out.out_regs.full = (next_size >= (FETCH_ADDR_FIFO_SIZE - 1));
  out.out_regs.empty = (next_size == 0);
}

namespace {

class FetchAddressFifoModel {
public:
  void seq_read(const fetch_address_FIFO_in &inp, fetch_address_FIFO_read_data &rd) const {
    (void)inp;
    std::memset(&rd, 0, sizeof(rd));
    rd.size = size_;
    rd.head_valid = (size_ > 0);
    if (rd.head_valid) {
      rd.head_entry = entries_[0];
    }
  }

  void build_next_read_data(const fetch_address_FIFO_read_data &cur,
                            const FetchAddrCombOut &comb_out,
                            fetch_address_FIFO_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_fifo) {
      next_rd = {};
    }
    const fetch_addr_fifo_size_t size_before_ops = next_rd.size;
    if (comb_out.push_en) {
      if (next_rd.size >= FETCH_ADDR_FIFO_SIZE) {
        std::printf("[fetch_address_FIFO] ERROR: fifo overflow\n");
        std::exit(1);
      }
      if (next_rd.size == 0) {
        next_rd.head_entry = comb_out.push_data;
        next_rd.head_valid = true;
      }
      ++next_rd.size;
    }
    if (comb_out.pop_en) {
      if (next_rd.size == 0) {
        std::printf("[fetch_address_FIFO] ERROR: fifo underflow on read\n");
        std::exit(1);
      }
      --next_rd.size;
      if (next_rd.size == 0) {
        next_rd.head_entry = 0;
        next_rd.head_valid = false;
      } else if (size_before_ops > 1) {
        next_rd.head_entry = 0;
        next_rd.head_valid = false;
      } else if (comb_out.push_en) {
        next_rd.head_entry = comb_out.push_data;
        next_rd.head_valid = true;
      }
    }
  }

  void comb_calc(const fetch_address_FIFO_in &inp,
                 const fetch_address_FIFO_read_data &rd,
                 fetch_address_FIFO_out &out,
                 fetch_address_FIFO_read_data &next_rd,
                 FetchAddrCombOut &step_req) const {
    FetchAddrCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    fetch_address_FIFO_comb(comb_in, step_req);
    out = step_req.out_regs;
    build_next_read_data(rd, step_req, next_rd);
  }

  void seq_write(const FetchAddrCombOut &req) {
    if (req.clear_fifo) {
      size_ = 0;
      std::memset(entries_, 0, sizeof(entries_));
    }
    if (req.push_en) {
      if (size_ >= FETCH_ADDR_FIFO_SIZE) {
        std::printf("[fetch_address_FIFO] ERROR: fifo overflow\n");
        std::exit(1);
      }
      entries_[size_++] = req.push_data;
    }
    if (req.pop_en) {
      if (size_ == 0) {
        std::printf("[fetch_address_FIFO] ERROR: fifo underflow on read\n");
        std::exit(1);
      }
      for (fetch_addr_fifo_size_t i = 1; i < size_; ++i) {
        entries_[i - 1] = entries_[i];
      }
      --size_;
      entries_[size_] = 0;
    }
  }

private:
  fetch_addr_t entries_[FETCH_ADDR_FIFO_SIZE]{};
  fetch_addr_fifo_size_t size_ = 0;
};

FetchAddressFifoModel g_fetch_addr_fifo_model;

} // namespace

void fetch_address_FIFO_seq_read(struct fetch_address_FIFO_in *in,
                                 struct fetch_address_FIFO_read_data *rd) {
  assert(in);
  assert(rd);
  g_fetch_addr_fifo_model.seq_read(*in, *rd);
}

void fetch_address_FIFO_comb_calc(struct fetch_address_FIFO_in *in,
                                  const struct fetch_address_FIFO_read_data *rd,
                                  struct fetch_address_FIFO_out *out,
                                  struct fetch_address_FIFO_read_data *next_rd,
                                  FetchAddrCombOut *step_req) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  assert(step_req);
  g_fetch_addr_fifo_model.comb_calc(*in, *rd, *out, *next_rd, *step_req);
}

void fetch_address_FIFO_seq_write(const FetchAddrCombOut *req) {
  assert(req);
  g_fetch_addr_fifo_model.seq_write(*req);
}
