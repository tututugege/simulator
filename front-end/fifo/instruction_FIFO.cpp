#include "front_fifo.h"
#include "../train_IO.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
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
    const instruction_FIFO_entry &read_entry =
        has_data_before_read ? in.rd.entries[0] : write_entry;
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
    std::memset(&rd, 0, sizeof(instruction_FIFO_read_data));
    rd.size = static_cast<uint8_t>(fifo_.size());
    std::queue<instruction_FIFO_entry> snapshot = fifo_;
    for (uint8_t i = 0; i < rd.size; ++i) {
      rd.entries[i] = snapshot.front();
      snapshot.pop();
    }
  }

  void build_next_read_data(const instruction_FIFO_read_data &cur,
                            const InstructionCombOut &comb_out,
                            instruction_FIFO_read_data &next_rd) const {
    next_rd = cur;
    if (comb_out.clear_fifo) {
      next_rd.size = 0;
    }
    if (comb_out.push_en) {
      if (next_rd.size >= INSTRUCTION_FIFO_SIZE) {
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo.size() >= INSTRUCTION_FIFO_SIZE\n");
        std::exit(1);
      }
      next_rd.entries[next_rd.size++] = comb_out.push_entry;
    }
    if (comb_out.pop_en) {
      if (next_rd.size == 0) {
        std::printf("[INSTRUCTION_FIFO_TOP] ERROR!!: fifo underflow on read\n");
        std::exit(1);
      }
      for (uint8_t i = 1; i < next_rd.size; ++i) {
        next_rd.entries[i - 1] = next_rd.entries[i];
      }
      --next_rd.size;
      next_rd.entries[next_rd.size] = instruction_FIFO_entry{};
    }
  }

  void comb_calc(const instruction_FIFO_in &inp,
                 const instruction_FIFO_read_data &rd,
                 instruction_FIFO_out &out,
                 instruction_FIFO_read_data &next_rd) const {
    InstructionCombIn comb_in{};
    comb_in.inp = inp;
    comb_in.rd = rd;

    InstructionCombOut comb_out{};
    instruction_FIFO_comb(comb_in, comb_out);
    out = comb_out.out_regs;
    build_next_read_data(rd, comb_out, next_rd);
  }

  void seq_write(const instruction_FIFO_read_data &next_rd) {
    clear_fifo(fifo_);
    for (uint8_t i = 0; i < next_rd.size; ++i) {
      fifo_.push(next_rd.entries[i]);
    }
  }

private:
  void clear_fifo(std::queue<instruction_FIFO_entry> &queue_ref) {
    while (!queue_ref.empty()) {
      queue_ref.pop();
    }
  }

  std::queue<instruction_FIFO_entry> fifo_;
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
                                struct instruction_FIFO_read_data *next_rd) {
  assert(in);
  assert(rd);
  assert(out);
  assert(next_rd);
  g_instruction_fifo.comb_calc(*in, *rd, *out, *next_rd);
}

void instruction_FIFO_seq_write(const struct instruction_FIFO_read_data *next_rd) {
  assert(next_rd);
  g_instruction_fifo.seq_write(*next_rd);
}
