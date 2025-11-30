#include "front_fifo.h"
#include <array>
#include <assert.h>
#include <iostream>
#include <queue>

struct FIFO_entry {
  std::array<uint32_t, FETCH_WIDTH> instructions;
  std::array<bool, FETCH_WIDTH> page_fault_inst;
  std::array<bool, FETCH_WIDTH> inst_valid;
  std::array<uint8_t, FETCH_WIDTH> predecode_type; // for predecode result
  std::array<uint32_t, FETCH_WIDTH> predecode_target_address;
};

static std::queue<FIFO_entry> fifo;

void instruction_FIFO_top(struct instruction_FIFO_in *in,
                          struct instruction_FIFO_out *out) {
  // clear FIFO
  if (in->reset) {
    while (!fifo.empty()) {
      fifo.pop();
    }
    out->full = false;
    out->empty = true;
    return;
  }

  if (in->refetch) {
    while (!fifo.empty()) {
      fifo.pop();
    }
    out->full = false;
    out->empty = true;
  }
  if (fifo.size() > INSTRUCTION_FIFO_SIZE)
    assert(0);

  // std::cout << "FIFO size:" << fifo.size() << std::endl;

  // if FIFO is not full and icache has new data
  if (in->write_enable) {
    if (fifo.size() >= INSTRUCTION_FIFO_SIZE) {
      assert(0); // should not reach here
    }
    FIFO_entry entry;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      entry.instructions[i] = in->fetch_group[i];
      entry.page_fault_inst[i] = in->page_fault_inst[i];
      entry.inst_valid[i] = in->inst_valid[i];
      entry.predecode_type[i] = in->predecode_type[i];
      entry.predecode_target_address[i] = in->predecode_target_address[i];
    }
    fifo.push(entry);
  }

  // output data
  if (in->read_enable && !fifo.empty()) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->instructions[i] = fifo.front().instructions[i];
      out->page_fault_inst[i] = fifo.front().page_fault_inst[i];
      out->inst_valid[i] = fifo.front().inst_valid[i];
      out->predecode_type[i] = fifo.front().predecode_type[i];
      out->predecode_target_address[i] = fifo.front().predecode_target_address[i];
    }
    fifo.pop();
    out->FIFO_valid = true;
  } else {
    out->FIFO_valid = false;
  }

  out->empty = fifo.empty();
  out->full = fifo.size() == INSTRUCTION_FIFO_SIZE;
}
