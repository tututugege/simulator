#pragma once

#include "config.h"
#include <cstdint>

struct InstructionBufferEntry {
  bool valid = false;
  uint32_t inst = 0;
  bool page_fault_inst = false;
  uint32_t ftq_idx = 0;
  uint32_t ftq_offset = 0;
  bool ftq_is_last = false;
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
