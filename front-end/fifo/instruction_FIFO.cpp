#include "../front_IO.h"
#include "../frontend.h"
#include <array>
#include <assert.h>
#include <queue>

#define FIFO_SIZE 10000

struct FIFO_entry {
  std::array<uint32_t, FETCH_WIDTH> instructions;
  std::array<bool, FETCH_WIDTH> page_fault_inst;
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
  if (fifo.size() > FIFO_SIZE)
    assert(0);

  // if FIFO is not full and icache has new data
  if (fifo.size() < FIFO_SIZE && in->write_enable) {
    FIFO_entry entry;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      entry.instructions[i] = in->fetch_group[i];
      entry.page_fault_inst[i] = in->page_fault_inst[i];
    }
    fifo.push(entry);
  }

  // output data
  if (!fifo.empty() && in->read_enable) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->instructions[i] = fifo.front().instructions[i];
      out->page_fault_inst[i] = fifo.front().page_fault_inst[i];
    }
    fifo.pop();
    out->FIFO_valid = true;
  } else {
    out->FIFO_valid = false;
  }
  out->empty = fifo.empty();
  out->full = fifo.size() == FIFO_SIZE;
}
