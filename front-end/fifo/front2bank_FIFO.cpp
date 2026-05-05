#include "front_fifo.h"
#include "../train_IO.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

constexpr uint32_t bit_mask_u32(int bits) {
  return (bits >= 32) ? 0xffffffffu : ((1u << bits) - 1u);
}

void front2back_FIFO_comb(const Front2BackCombIn &in,
                          Front2BackCombOut &out) {
  std::memset(&out, 0, sizeof(Front2BackCombOut));
  out.out_regs.full = false;
  out.out_regs.empty = true;
  out.out_regs.front2back_FIFO_valid = false;

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

  front2back_FIFO_entry write_entry{};
  if (do_write) {
    constexpr uint32_t kPcpnMask = bit_mask_u32(pcpn_t_BITS);
    constexpr uint32_t kTageIdxMask = bit_mask_u32(tage_idx_t_BITS);
    constexpr uint32_t kTageTagMask = bit_mask_u32(tage_tag_t_BITS);
    for (int i = 0; i < FETCH_WIDTH; i++) {
      write_entry.fetch_group[i] = in.inp.fetch_group[i];
      write_entry.page_fault_inst[i] = in.inp.page_fault_inst[i];
      write_entry.inst_valid[i] = in.inp.inst_valid[i];
      write_entry.predict_dir_corrected[i] = in.inp.predict_dir_corrected[i];
      write_entry.predict_base_pc[i] = in.inp.predict_base_pc[i];
      write_entry.alt_pred[i] = in.inp.alt_pred[i];
      write_entry.altpcpn[i] =
          static_cast<uint8_t>(in.inp.altpcpn[i] & kPcpnMask);
      write_entry.pcpn[i] = static_cast<uint8_t>(in.inp.pcpn[i] & kPcpnMask);
      for (int j = 0; j < TN_MAX; j++) {
        write_entry.tage_idx[i][j] = in.inp.tage_idx[i][j] & kTageIdxMask;
        write_entry.tage_tag[i][j] = in.inp.tage_tag[i][j] & kTageTagMask;
      }
      write_entry.sc_used[i] = in.inp.sc_used[i];
      write_entry.sc_pred[i] = in.inp.sc_pred[i];
      write_entry.sc_sum[i] = in.inp.sc_sum[i];
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        write_entry.sc_idx[i][t] = in.inp.sc_idx[i][t];
      }
      write_entry.loop_used[i] = in.inp.loop_used[i];
      write_entry.loop_hit[i] = in.inp.loop_hit[i];
      write_entry.loop_pred[i] = in.inp.loop_pred[i];
      write_entry.loop_idx[i] = in.inp.loop_idx[i];
      write_entry.loop_tag[i] = in.inp.loop_tag[i];
    }
    write_entry.predict_next_fetch_address_corrected =
        in.inp.predict_next_fetch_address_corrected;
  }

  if (do_read) {
    if (has_data_before_read && !in.rd.head_valid) {
      std::printf("[FRONT2BACK_FIFO_TOP] ERROR!!: missing head snapshot for same-cycle reread\n");
      std::exit(1);
    }
    constexpr uint32_t kPcpnMask = bit_mask_u32(pcpn_t_BITS);
    constexpr uint32_t kTageIdxMask = bit_mask_u32(tage_idx_t_BITS);
    constexpr uint32_t kTageTagMask = bit_mask_u32(tage_tag_t_BITS);
    const front2back_FIFO_entry &read_entry =
        has_data_before_read ? in.rd.head_entry : write_entry;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.out_regs.fetch_group[i] = read_entry.fetch_group[i];
      out.out_regs.page_fault_inst[i] = read_entry.page_fault_inst[i];
      out.out_regs.inst_valid[i] = read_entry.inst_valid[i];
      out.out_regs.predict_dir_corrected[i] = read_entry.predict_dir_corrected[i];
      out.out_regs.predict_base_pc[i] = read_entry.predict_base_pc[i];
      out.out_regs.alt_pred[i] = read_entry.alt_pred[i];
      out.out_regs.altpcpn[i] =
          static_cast<pcpn_t>(read_entry.altpcpn[i] & kPcpnMask);
      out.out_regs.pcpn[i] = static_cast<pcpn_t>(read_entry.pcpn[i] & kPcpnMask);
      for (int j = 0; j < TN_MAX; j++) {
        out.out_regs.tage_idx[i][j] =
            static_cast<tage_idx_t>(read_entry.tage_idx[i][j] & kTageIdxMask);
        out.out_regs.tage_tag[i][j] =
            static_cast<tage_tag_t>(read_entry.tage_tag[i][j] & kTageTagMask);
      }
      out.out_regs.sc_used[i] = read_entry.sc_used[i];
      out.out_regs.sc_pred[i] = read_entry.sc_pred[i];
      out.out_regs.sc_sum[i] = read_entry.sc_sum[i];
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        out.out_regs.sc_idx[i][t] = read_entry.sc_idx[i][t];
      }
      out.out_regs.loop_used[i] = read_entry.loop_used[i];
      out.out_regs.loop_hit[i] = read_entry.loop_hit[i];
      out.out_regs.loop_pred[i] = read_entry.loop_pred[i];
      out.out_regs.loop_idx[i] = read_entry.loop_idx[i];
      out.out_regs.loop_tag[i] = read_entry.loop_tag[i];
    }
    out.out_regs.predict_next_fetch_address_corrected =
        read_entry.predict_next_fetch_address_corrected;
    out.out_regs.front2back_FIFO_valid = true;
  }

  out.push_en = do_write;
  out.push_entry = write_entry;
  out.pop_en = do_read;

  int next_size = static_cast<int>(queue_size_before);
  next_size += out.push_en ? 1 : 0;
  next_size -= out.pop_en ? 1 : 0;
  if (next_size < 0) {
    next_size = 0;
  }
  out.out_regs.full = (next_size == FRONT2BACK_FIFO_SIZE);
  out.out_regs.empty = (next_size == 0);
}

namespace {

class Front2BackFifoModel {
public:
  void seq_read(const front2back_FIFO_in &inp, front2back_FIFO_read_data &rd) const {
    (void)inp;
    std::memset(&rd, 0, sizeof(rd));
    rd.size = size_;
    rd.head_valid = (size_ > 0);
    if (rd.head_valid) {
      rd.head_entry = entries_[0];
    }
  }

  void build_next_read_data(const front2back_FIFO_read_data &cur,
                            const Front2BackCombOut &comb_out,
                            front2back_FIFO_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_fifo) {
      next_rd = {};
    }
    const front2back_fifo_size_t size_before_ops = next_rd.size;
    if (comb_out.push_en) {
      if (next_rd.size >= FRONT2BACK_FIFO_SIZE) {
        std::printf("[FRONT2BACK_FIFO_TOP] ERROR!!: front2back_fifo.size() >= FRONT2BACK_FIFO_SIZE\n");
        std::exit(1);
      }
      if (next_rd.size == 0) {
        next_rd.head_entry = comb_out.push_entry;
        next_rd.head_valid = true;
      }
      ++next_rd.size;
    }
    if (comb_out.pop_en) {
      if (next_rd.size == 0) {
        std::printf("[FRONT2BACK_FIFO_TOP] ERROR!!: front2back_fifo underflow on read\n");
        std::exit(1);
      }
      --next_rd.size;
      if (next_rd.size == 0) {
        next_rd.head_entry = front2back_FIFO_entry{};
        next_rd.head_valid = false;
      } else if (size_before_ops > 1) {
        next_rd.head_entry = front2back_FIFO_entry{};
        next_rd.head_valid = false;
      } else if (comb_out.push_en) {
        next_rd.head_entry = comb_out.push_entry;
        next_rd.head_valid = true;
      }
    }
  }

  void comb_calc(const front2back_FIFO_in &inp,
                 const front2back_FIFO_read_data &rd,
                 front2back_FIFO_out &out,
                 front2back_FIFO_read_data &next_rd,
                 Front2BackCombOut &step_req) const {
    Front2BackCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    front2back_FIFO_comb(comb_in, step_req);
    out = step_req.out_regs;
    build_next_read_data(rd, step_req, next_rd);
  }

  void seq_write(const Front2BackCombOut &req) {
    if (req.clear_fifo) {
      size_ = 0;
      std::memset(entries_, 0, sizeof(entries_));
    }
    if (req.push_en) {
      if (size_ >= FRONT2BACK_FIFO_SIZE) {
        std::printf("[FRONT2BACK_FIFO_TOP] ERROR!!: front2back_fifo.size() >= FRONT2BACK_FIFO_SIZE\n");
        std::exit(1);
      }
      entries_[size_++] = req.push_entry;
    }
    if (req.pop_en) {
      if (size_ == 0) {
        std::printf("[FRONT2BACK_FIFO_TOP] ERROR!!: front2back_fifo underflow on read\n");
        std::exit(1);
      }
      for (front2back_fifo_size_t i = 1; i < size_; ++i) {
        entries_[i - 1] = entries_[i];
      }
      --size_;
      entries_[size_] = front2back_FIFO_entry{};
    }
  }

private:
  front2back_FIFO_entry entries_[FRONT2BACK_FIFO_SIZE]{};
  front2back_fifo_size_t size_ = 0;
};

Front2BackFifoModel g_front2back_fifo_model;

} // namespace

void front2back_FIFO_seq_read(struct front2back_FIFO_in *in,
                              struct front2back_FIFO_read_data *rd) {
  assert(in);
  assert(rd);
  g_front2back_fifo_model.seq_read(*in, *rd);
}

void front2back_FIFO_comb_calc(struct front2back_FIFO_in *in,
                               const struct front2back_FIFO_read_data *rd,
                               struct front2back_FIFO_out *out,
                               struct front2back_FIFO_read_data *next_rd,
                               Front2BackCombOut *step_req) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  assert(step_req);
  g_front2back_fifo_model.comb_calc(*in, *rd, *out, *next_rd, *step_req);
}

void front2back_FIFO_seq_write(const Front2BackCombOut *req) {
  assert(req);
  g_front2back_fifo_model.seq_write(*req);
}
