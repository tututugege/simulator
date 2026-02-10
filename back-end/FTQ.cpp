#include "FTQ.h"
#include <iostream>

FTQ::FTQ() {
  head = 0;
  tail = 0;
  count = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    entries[i].valid = false;
  }
}

int FTQ::alloc() {
  if (count >= FTQ_SIZE) {
    // Should be handled by flow control (stall)
    return -1;
  }
  int idx = tail;
  entries[idx].valid = true;
  // Initialize entry if needed, though decoder usually overwrites
  tail = (tail + 1) % FTQ_SIZE;
  count++;
  return idx;
}

FTQEntry &FTQ::get(int idx) {
  return entries[idx];
}


void FTQ::pop() {
  if (count > 0) {
    head = (head + 1) % FTQ_SIZE;
    count--;
  }
}

void FTQ::recover(int new_tail) {
  // Logic to handle wrap-around recovery is needed if we pass "absolute" index
  // But usually we pass the "ftq_idx" stored in the instruction which is the valid index.
  // If we want to roll back to a specific instruction's FTQ entry, we set tail to (idx + 1).
  
  // CAUTION: This simple logic assumes we recover TO a specific point. 
  // If new_tail is the index of the mispredicted branch's FTQ entry,
  // we want to keep it (if it contains valid info for that block) or discard AFTER it?
  // Usually, on misprediction, we might redirect fetch.
  // If the misprediction is within the block, that block is still valid up to the branch.
  
  // Implementation: Set tail to new_tail. Recalculate count.
  // This requires knowing the distance or just resetting logic.
  // For simplicity, let's assume external logic calculates the correct next tail pointer.
  // OR, more robustly:
  
  tail = new_tail;
  
  // Recalculate count is tricky with circular buffer without more info (like head).
  // But FTQ is specific: head is commit, tail is fetch.
  // On misprediction, head doesn't change. Tail moves back.
  if (tail >= head) {
      count = tail - head;
  } else {
      count = FTQ_SIZE - (head - tail);
  }
}

bool FTQ::is_full() {
    return count >= FTQ_SIZE;
}

bool FTQ::is_empty() {
    return count == 0;
}
