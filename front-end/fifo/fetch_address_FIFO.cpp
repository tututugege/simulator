#include "front_fifo.h"
#include "../train_IO.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
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
    out.out_regs.read_valid = true;
    out.out_regs.fetch_address =
        has_data_before_read ? in.rd.entries[0] : in.inp.fetch_address;
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
    std::memset(&rd, 0, sizeof(fetch_address_FIFO_read_data));
    rd.size = static_cast<uint8_t>(fifo_.size());
    std::queue<uint32_t> snapshot = fifo_;
    for (uint8_t i = 0; i < rd.size; ++i) {
      rd.entries[i] = snapshot.front();
      snapshot.pop();
    }
  }

  void build_next_read_data(const fetch_address_FIFO_read_data &cur,
                            const FetchAddrCombOut &comb_out,
                            fetch_address_FIFO_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_fifo) {
      next_rd.size = 0;
    }
    if (comb_out.push_en) {
      if (next_rd.size >= FETCH_ADDR_FIFO_SIZE) {
        std::printf("[fetch_address_FIFO] ERROR: fifo overflow\n");
        std::exit(1);
      }
      next_rd.entries[next_rd.size++] = comb_out.push_data;
    }
    if (comb_out.pop_en) {
      if (next_rd.size == 0) {
        std::printf("[fetch_address_FIFO] ERROR: fifo underflow on read\n");
        std::exit(1);
      }
      for (uint8_t i = 1; i < next_rd.size; ++i) {
        next_rd.entries[i - 1] = next_rd.entries[i];
      }
      --next_rd.size;
      next_rd.entries[next_rd.size] = 0;
    }
  }

  void comb_calc(const fetch_address_FIFO_in &inp,
                 const fetch_address_FIFO_read_data &rd,
                 fetch_address_FIFO_out &out,
                 fetch_address_FIFO_read_data &next_rd) const {
    FetchAddrCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    FetchAddrCombOut comb_out{};
    fetch_address_FIFO_comb(comb_in, comb_out);
    out = comb_out.out_regs;
    build_next_read_data(rd, comb_out, next_rd);
  }

  void seq_write(const fetch_address_FIFO_read_data &next_rd) {
    clear_queue(fifo_);
    for (uint8_t i = 0; i < next_rd.size; ++i) {
      fifo_.push(next_rd.entries[i]);
    }
  }

private:
  void clear_queue(std::queue<uint32_t> &queue_ref) {
    while (!queue_ref.empty()) {
      queue_ref.pop();
    }
  }

  std::queue<uint32_t> fifo_;
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
                                  struct fetch_address_FIFO_read_data *next_rd) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  g_fetch_addr_fifo_model.comb_calc(*in, *rd, *out, *next_rd);
}

void fetch_address_FIFO_seq_write(const struct fetch_address_FIFO_read_data *next_rd) {
  assert(next_rd);
  g_fetch_addr_fifo_model.seq_write(*next_rd);
}
