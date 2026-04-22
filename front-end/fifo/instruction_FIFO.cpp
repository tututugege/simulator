#include "front_fifo.h"
#include "../train_IO.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "frontend.h"

void instruction_FIFO_comb(const InstructionCombIn &in,
                           InstructionCombOut &out) {
  std::memset(&out, 0, sizeof(InstructionCombOut));
  out.out_regs.full = false;
  out.out_regs.empty = true;
  out.out_regs.FIFO_valid = false;

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

  instruction_FIFO_entry write_entry{};
  if (do_write) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      write_entry.instructions[i] = in.inp.fetch_group[i];
      write_entry.pc[i] = in.inp.pc[i];
      write_entry.page_fault_inst[i] = in.inp.page_fault_inst[i];
      write_entry.inst_valid[i] = in.inp.inst_valid[i];
      write_entry.predecode_type[i] = in.inp.predecode_type[i];
      write_entry.predecode_target_address[i] = in.inp.predecode_target_address[i];
    }
    write_entry.seq_next_pc = in.inp.seq_next_pc;
  }

  if (do_read) {
    if (has_data_before_read && !in.rd.head_valid) {
      std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: missing head snapshot for same-cycle reread\n");
      std::exit(1);
    }
    const instruction_FIFO_entry &read_entry =
        has_data_before_read ? in.rd.head_entry : write_entry;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.out_regs.instructions[i] = read_entry.instructions[i];
      out.out_regs.pc[i] = read_entry.pc[i];
      out.out_regs.page_fault_inst[i] = read_entry.page_fault_inst[i];
      out.out_regs.inst_valid[i] = read_entry.inst_valid[i];
      out.out_regs.predecode_type[i] = read_entry.predecode_type[i];
      out.out_regs.predecode_target_address[i] = read_entry.predecode_target_address[i];
    }
    out.out_regs.seq_next_pc = read_entry.seq_next_pc;
    out.out_regs.FIFO_valid = true;
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
  out.out_regs.empty = (next_size == 0);
  out.out_regs.full = (next_size == INSTRUCTION_FIFO_SIZE);
}

namespace {

class InstructionFifoModel {
public:
  void seq_read(const instruction_FIFO_in &inp, instruction_FIFO_read_data &rd) const {
    (void)inp;
    std::memset(&rd, 0, sizeof(rd));
    rd.size = size_;
    rd.head_valid = (size_ > 0);
    if (rd.head_valid) {
      rd.head_entry = entries_[0];
    }
  }

  void build_next_read_data(const instruction_FIFO_read_data &cur,
                            const InstructionCombOut &comb_out,
                            instruction_FIFO_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_fifo) {
      next_rd = {};
    }
    const instruction_fifo_size_t size_before_ops = next_rd.size;
    if (comb_out.push_en) {
      if (next_rd.size >= INSTRUCTION_FIFO_SIZE) {
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo.size() >= INSTRUCTION_FIFO_SIZE\n");
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
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo underflow on read\n");
        std::exit(1);
      }
      --next_rd.size;
      if (next_rd.size == 0) {
        next_rd.head_entry = instruction_FIFO_entry{};
        next_rd.head_valid = false;
      } else if (size_before_ops > 1) {
        next_rd.head_entry = instruction_FIFO_entry{};
        next_rd.head_valid = false;
      } else if (comb_out.push_en) {
        next_rd.head_entry = comb_out.push_entry;
        next_rd.head_valid = true;
      }
    }
  }

  void comb_calc(const instruction_FIFO_in &inp,
                 const instruction_FIFO_read_data &rd,
                 instruction_FIFO_out &out,
                 instruction_FIFO_read_data &next_rd,
                 InstructionCombOut &step_req) const {
    InstructionCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    instruction_FIFO_comb(comb_in, step_req);
    out = step_req.out_regs;
    build_next_read_data(rd, step_req, next_rd);
  }

  void seq_write(const InstructionCombOut &req) {
    if (req.clear_fifo) {
      size_ = 0;
      std::memset(entries_, 0, sizeof(entries_));
    }
    if (req.push_en) {
      if (size_ >= INSTRUCTION_FIFO_SIZE) {
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo.size() >= INSTRUCTION_FIFO_SIZE\n");
        std::exit(1);
      }
      entries_[size_++] = req.push_entry;
    }
    if (req.pop_en) {
      if (size_ == 0) {
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo underflow on read\n");
        std::exit(1);
      }
      for (instruction_fifo_size_t i = 1; i < size_; ++i) {
        entries_[i - 1] = entries_[i];
      }
      --size_;
      entries_[size_] = instruction_FIFO_entry{};
    }
  }

private:
  instruction_FIFO_entry entries_[INSTRUCTION_FIFO_SIZE]{};
  instruction_fifo_size_t size_ = 0;
};

InstructionFifoModel g_instruction_fifo;

} // namespace

void instruction_FIFO_seq_read(struct instruction_FIFO_in *in,
                               struct instruction_FIFO_read_data *rd) {
  assert(in);
  assert(rd);
  g_instruction_fifo.seq_read(*in, *rd);
}

void instruction_FIFO_comb_calc(struct instruction_FIFO_in *in,
                                const struct instruction_FIFO_read_data *rd,
                                struct instruction_FIFO_out *out,
                                struct instruction_FIFO_read_data *next_rd,
                                InstructionCombOut *step_req) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  assert(step_req);
  g_instruction_fifo.comb_calc(*in, *rd, *out, *next_rd, *step_req);
}

void instruction_FIFO_seq_write(const InstructionCombOut *req) {
  assert(req);
  g_instruction_fifo.seq_write(*req);
}
