#include "config.h"
#include <LSU.h>
void LDQ::init() {}
void LDQ::comb() {}

// 分配 写入 提交
void LDQ::seq() {
  // 分配
  for (int i = 0; i < INST_WAY; i++) {
    if (in.alloc[i].valid) {
      entry[enq_ptr] = in.alloc[i];
      enq_ptr = (enq_ptr + 1) % LDQ_NUM;
      count++;
    }
  }

  // 写入
  if (in.write.valid) {
    for (int i = deq_ptr; i < enq_ptr; i++) {
      if (entry[i].rob_idx == in.write.rob_idx) {
        entry[i].addr = in.write.addr;
        break;
      }
    }
  }

  // 提交
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit[i]) {
      entry[deq_ptr].valid = false;
      deq_ptr = (deq_ptr + 1) % LDQ_NUM;
    }
  }
}
