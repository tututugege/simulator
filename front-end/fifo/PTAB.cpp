#include "front_fifo.h"
#include "../train_IO.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "frontend.h"

constexpr uint32_t bit_mask_u32(int bits) {
  return (bits >= 32) ? 0xffffffffu : ((1u << bits) - 1u);
}

void PTAB_comb(const PtabCombIn &in, PtabCombOut &out) {
  std::memset(&out, 0, sizeof(PtabCombOut));
  out.out_regs.full = false;
  out.out_regs.empty = true;
  out.out_regs.dummy_entry = false;

  if (in.inp.reset) {
    out.clear_ptab = true;
    return;
  }

  bool has_data_before_read = (in.rd.size > 0);
  uint8_t queue_size_before = in.rd.size;
  if (in.inp.refetch) {
    out.clear_ptab = true;
    has_data_before_read = false;
    queue_size_before = 0;
  }

  const bool do_write = in.inp.write_enable;
  const bool push_dummy = in.inp.write_enable && in.inp.need_mini_flush;
  const bool do_read = in.inp.read_enable && (has_data_before_read || do_write || push_dummy);

  PTAB_entry write_entry{};
  if (do_write) {
    constexpr uint32_t kPcpnMask = bit_mask_u32(pcpn_t_BITS);
    constexpr uint32_t kTageIdxMask = bit_mask_u32(tage_idx_t_BITS);
    constexpr uint32_t kTageTagMask = bit_mask_u32(tage_tag_t_BITS);
    for (int i = 0; i < FETCH_WIDTH; i++) {
      write_entry.predict_dir[i] = in.inp.predict_dir[i];
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
    write_entry.predict_next_fetch_address = in.inp.predict_next_fetch_address;
    write_entry.need_mini_flush = in.inp.need_mini_flush;
    write_entry.dummy_entry = false;
  }

  PTAB_entry dummy_entry{};
  if (push_dummy) {
    std::memset(&dummy_entry, 0, sizeof(dummy_entry));
    dummy_entry.dummy_entry = true;
  }

  if (do_read) {
    if (has_data_before_read && !in.rd.head_valid) {
      std::printf("[PTAB_TOP] ERROR!!: missing head snapshot for same-cycle reread\n");
      std::exit(1);
    }
    constexpr uint32_t kPcpnMask = bit_mask_u32(pcpn_t_BITS);
    constexpr uint32_t kTageIdxMask = bit_mask_u32(tage_idx_t_BITS);
    constexpr uint32_t kTageTagMask = bit_mask_u32(tage_tag_t_BITS);
    const PTAB_entry &read_entry =
        has_data_before_read ? in.rd.head_entry : write_entry;
    out.out_regs.dummy_entry = read_entry.dummy_entry;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.out_regs.predict_dir[i] = read_entry.predict_dir[i];
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
    out.out_regs.predict_next_fetch_address = read_entry.predict_next_fetch_address;
  }

  out.push_write_en = do_write;
  out.push_write_entry = write_entry;
  out.push_dummy_en = push_dummy;
  out.push_dummy_entry = dummy_entry;
  out.pop_en = do_read;

  int next_size = static_cast<int>(queue_size_before);
  next_size += out.push_write_en ? 1 : 0;
  next_size += out.push_dummy_en ? 1 : 0;
  next_size -= out.pop_en ? 1 : 0;
  if (next_size < 0) {
    next_size = 0;
  }
  out.out_regs.full = (next_size >= (PTAB_SIZE - 1));
  out.out_regs.empty = (next_size == 0);
}

namespace {

class PTABModel {
public:
  void seq_read(const PTAB_in &inp, PTAB_read_data &rd) const {
    (void)inp;
    std::memset(&rd, 0, sizeof(rd));
    rd.size = size_;
    rd.head_valid = (size_ > 0);
    if (rd.head_valid) {
      rd.head_entry = entries_[0];
    }
  }

  void build_next_read_data(const PTAB_read_data &cur, const PtabCombOut &comb_out,
                            PTAB_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_ptab) {
      next_rd = {};
    }
    const ptab_size_t size_before_ops = next_rd.size;
    bool first_push_recorded = false;
    PTAB_entry first_push_entry{};
    if (comb_out.push_write_en) {
      if (next_rd.size >= PTAB_SIZE) {
        std::printf("[PTAB_TOP] ERROR!!: ptab.size() >= PTAB_SIZE\n");
        std::exit(1);
      }
      if (next_rd.size == 0) {
        next_rd.head_entry = comb_out.push_write_entry;
        next_rd.head_valid = true;
      }
      first_push_entry = comb_out.push_write_entry;
      first_push_recorded = true;
      ++next_rd.size;
    }
    if (comb_out.push_dummy_en) {
      if (next_rd.size >= PTAB_SIZE) {
        std::printf("[PTAB_TOP] dummy entry push ERROR!!: ptab.size() >= PTAB_SIZE\n");
        std::exit(1);
      }
      if (next_rd.size == 0) {
        next_rd.head_entry = comb_out.push_dummy_entry;
        next_rd.head_valid = true;
      }
      if (!first_push_recorded) {
        first_push_entry = comb_out.push_dummy_entry;
        first_push_recorded = true;
      }
      ++next_rd.size;
    }
    if (comb_out.pop_en) {
      if (next_rd.size == 0) {
        std::printf("[PTAB_TOP] ERROR!!: ptab underflow on read\n");
        std::exit(1);
      }
      --next_rd.size;
      if (next_rd.size == 0) {
        next_rd.head_entry = PTAB_entry{};
        next_rd.head_valid = false;
      } else if (size_before_ops > 1) {
        next_rd.head_entry = PTAB_entry{};
        next_rd.head_valid = false;
      } else if (first_push_recorded) {
        next_rd.head_entry = first_push_entry;
        next_rd.head_valid = true;
      }
    }
  }

  void comb_calc(const PTAB_in &inp, const PTAB_read_data &rd, PTAB_out &out,
                 PTAB_read_data &next_rd, PtabCombOut &step_req) const {
    PtabCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    PTAB_comb(comb_in, step_req);
    out = step_req.out_regs;
    build_next_read_data(rd, step_req, next_rd);
  }

  void seq_write(const PtabCombOut &req) {
    if (req.clear_ptab) {
      size_ = 0;
      std::memset(entries_, 0, sizeof(entries_));
    }
    if (req.push_write_en) {
      if (size_ >= PTAB_SIZE) {
        std::printf("[PTAB_TOP] ERROR!!: ptab.size() >= PTAB_SIZE\n");
        std::exit(1);
      }
      entries_[size_++] = req.push_write_entry;
    }
    if (req.push_dummy_en) {
      if (size_ >= PTAB_SIZE) {
        std::printf("[PTAB_TOP] dummy entry push ERROR!!: ptab.size() >= PTAB_SIZE\n");
        std::exit(1);
      }
      entries_[size_++] = req.push_dummy_entry;
    }
    if (req.pop_en) {
      if (size_ == 0) {
        std::printf("[PTAB_TOP] ERROR!!: ptab underflow on read\n");
        std::exit(1);
      }
      for (ptab_size_t i = 1; i < size_; ++i) {
        entries_[i - 1] = entries_[i];
      }
      --size_;
      entries_[size_] = PTAB_entry{};
    }
  }

  bool peek_mini_flush() {
    if (size_ == 0) {
      return false;
    }
    DEBUG_LOG_SMALL_4("ptab_peeking for pc=%x,need_mini_flush=%d\n",
                      entries_[0].predict_base_pc[0], entries_[0].need_mini_flush);
    return entries_[0].need_mini_flush;
  }

private:
  PTAB_entry entries_[PTAB_SIZE]{};
  ptab_size_t size_ = 0;
};

PTABModel g_ptab_model;

} // namespace

void PTAB_seq_read(struct PTAB_in *in, struct PTAB_read_data *rd) {
  assert(in);
  assert(rd);
  g_ptab_model.seq_read(*in, *rd);
}

void PTAB_comb_calc(struct PTAB_in *in, const struct PTAB_read_data *rd,
                    struct PTAB_out *out, struct PTAB_read_data *next_rd,
                    PtabCombOut *step_req) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  assert(step_req);
  g_ptab_model.comb_calc(*in, *rd, *out, *next_rd, *step_req);
}

void PTAB_seq_write(const PtabCombOut *req) {
  assert(req);
  g_ptab_model.seq_write(*req);
}
