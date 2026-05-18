#include "InstructionBuffer.h"
#include "util.h"

void InstructionBuffer::init() { clear(); }

const InstructionBufferEntry &InstructionBuffer::peek(int offset) const {
  Assert(offset >= 0 && offset < count_r &&
         "InstructionBuffer::peek offset out of range");
  int idx = (head_r + offset) % IDU_INST_BUFFER_SIZE;
  return entries[idx];
}

void InstructionBuffer::pop_front(int n) {
  if (n <= 0) {
    return;
  }
  int actual = (n > count_r) ? count_r : n;
  for (int i = 0; i < actual; i++) {
    entries[head_r] = {};
    head_r = (head_r + 1) % IDU_INST_BUFFER_SIZE;
  }
  count_r -= actual;
}

void InstructionBuffer::push_back(const InstructionBufferEntry &entry) {
  Assert(count_r < IDU_INST_BUFFER_SIZE && "InstructionBuffer overflow");
  entries[tail_r] = entry;
  tail_r = (tail_r + 1) % IDU_INST_BUFFER_SIZE;
  count_r++;
}

void InstructionBuffer::clear() {
  head_r = 0;
  tail_r = 0;
  count_r = 0;
  for (int i = 0; i < IDU_INST_BUFFER_SIZE; i++) {
    entries[i] = {};
  }
}
