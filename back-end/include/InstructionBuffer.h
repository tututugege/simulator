#pragma once

#include "config.h"
#include <cstdint>

struct InstructionBufferEntry {
  wire<1> valid;
  wire<32> inst;
  wire<32> pc;
  wire<1> page_fault_inst;
  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> ftq_is_last;

  InstructionBufferEntry() {
    valid = 0;
    inst = 0;
    pc = 0;
    page_fault_inst = 0;
    ftq_idx = 0;
    ftq_offset = 0;
    ftq_is_last = 0;
  }
};

class InstructionBuffer {
public:
  void init();
  int count() const { return count_r; }
  int free_slots() const { return IDU_INST_BUFFER_SIZE - count_r; }
  bool can_accept(int incoming_num) const {
    return incoming_num <= free_slots();
  }

  const InstructionBufferEntry &peek(int offset) const;
  void pop_front(int n);
  void push_back(const InstructionBufferEntry &entry);
  void clear();

private:
  InstructionBufferEntry entries[IDU_INST_BUFFER_SIZE];
  int head_r = 0;
  int tail_r = 0;
  int count_r = 0;
};
