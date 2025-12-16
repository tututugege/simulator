#include "TOP.h"
#include <Cache.h>
#include <STQ.h>
#include <config.h>
#include <cstdint>
#include <iostream>
#include <util.h>

extern Back_Top back;
extern Cache cache;

void STQ::comb() {
  out.stq2front->fence_stall = (state == FENCE);
  int num = count;
  static int last_ch = 0;

  for (int i = 0; i < 2; i++) {
    if (!in.dis2stq->valid[i]) {
      out.stq2dis->ready[i] = true;
    } else {
      if (num < STQ_NUM) {
        out.stq2dis->ready[i] = true;
        num++;
      } else {
        out.stq2dis->ready[i] = false;
      }
    }
  }

  if (entry[deq_ptr].valid && entry[deq_ptr].commit) {
    extern uint32_t *p_memory;
    uint32_t wdata = entry[deq_ptr].data;
    uint32_t waddr = entry[deq_ptr].addr;
    uint32_t wstrb;
    if (entry[deq_ptr].size == 0b00)
      wstrb = 0b1;
    else if (entry[deq_ptr].size == 0b01)
      wstrb = 0b11;
    else
      wstrb = 0b1111;

    int offset = entry[deq_ptr].addr & 0x3;
    wstrb = wstrb << offset;
    wdata = wdata << (offset * 8);

    uint32_t old_data = p_memory[waddr / 4];
    uint32_t mask = 0;
    if (wstrb & 0b1)
      mask |= 0xFF;
    if (wstrb & 0b10)
      mask |= 0xFF00;
    if (wstrb & 0b100)
      mask |= 0xFF0000;
    if (wstrb & 0b1000)
      mask |= 0xFF000000;

    cache.cache_access(waddr);

    p_memory[waddr / 4] = (mask & wdata) | (~mask & old_data);

    if (waddr == UART_BASE) {
      char temp;
      temp = wdata & 0x000000ff;
      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] & 0xffffff00;

      if (temp != 27)
        cout << temp;
      // if (temp == '?' && !perf.perf_start) {
      //   cout << " perf counter start" << endl;
      //   perf.perf_start = true;
      //   perf.perf_reset();
      // } else if (temp == '#' && perf.perf_start && last_ch == '?') {
      //   perf.perf_print();
      //   exit(0);
      // }
      // last_ch = temp;
    }

    if (waddr == 0x10000001 && (entry[deq_ptr].data & 0x000000ff) == 7) {
      p_memory[0xc201004 / 4] = 0xa;
      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] & 0xfff0ffff;
    }
    if (waddr == 0x10000001 && (entry[deq_ptr].data & 0x000000ff) == 5) {
      p_memory[0x10000000 / 4] =
          p_memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
    }
    if (waddr == 0xc201004 && (entry[deq_ptr].data & 0x000000ff) == 0xa) {
      p_memory[0xc201004 / 4] = 0x0;
    }

    if (MEM_LOG) {
      cout << "store data " << hex << ((mask & wdata) | (~mask & old_data))
           << " in " << (waddr & 0xFFFFFFFC) << endl;
    }

    entry[deq_ptr].valid = false;
    entry[deq_ptr].commit = false;
    LOOP_INC(deq_ptr, STQ_NUM);
    count--;
    commit_count--;
  }

  // commit标记为可执行
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid &&
        (is_store(in.rob_commit->commit_entry[i].uop)) &&
        !in.rob_commit->commit_entry[i].uop.page_fault_store) {
      entry[commit_ptr].commit = true;
      commit_count++;
      LOOP_INC(commit_ptr, STQ_NUM);
    }
  }

  if (state == NORMAL) {
    if (in.rob_bcast->fence && count != 0) {
      state = FENCE;
    }
  } else if (state == FENCE) {
    if (count == 0) {
      state = NORMAL;
    }
  }
}

void STQ::seq() {

  // 入队
  for (int i = 0; i < 2; i++) {
    if (in.dis2stq->dis_fire[i] && in.dis2stq->valid[i]) {
      entry[enq_ptr].tag = in.dis2stq->tag[i];
      entry[enq_ptr].valid = true;
      entry[enq_ptr].addr_valid = false;
      entry[enq_ptr].data_valid = false;
      count++;
      LOOP_INC(enq_ptr, STQ_NUM);
    }
  }

  // 地址数据写入 若项无效说明被br清除
  Inst_uop *inst = &in.exe2stq->addr_entry.uop;
  int idx = inst->stq_idx;
  if (in.exe2stq->addr_entry.valid && entry[idx].valid) {
    entry[idx].addr = inst->result;
    entry[idx].size = inst->func3;
    entry[idx].addr_valid = true;
  }

  inst = &in.exe2stq->data_entry.uop;
  idx = inst->stq_idx;

  if (in.exe2stq->data_entry.valid && entry[idx].valid) {
    entry[idx].data = inst->result;
    entry[idx].data_valid = true;
  }

  // 分支清空
  if (in.dec_bcast->mispred) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && !entry[i].commit &&
          (in.dec_bcast->br_mask & (1 << entry[i].tag))) {
        entry[i].valid = false;
        entry[i].commit = false;
        count--;
        LOOP_DEC(enq_ptr, STQ_NUM);
      }
    }
  }

  if (in.rob_bcast->flush) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && !entry[i].commit) {
        entry[i].valid = false;
        entry[i].commit = false;
        count--;
        LOOP_DEC(enq_ptr, STQ_NUM);
      }
    }
  }

  out.stq2dis->stq_idx = enq_ptr;
}

extern uint32_t *p_memory;
void STQ::st2ld_fwd(uint32_t addr, uint32_t &data, int rob_idx,
                    bool &stall_load) {

  int i = deq_ptr;
  int count = commit_count;
  while (count != 0) {
    if ((entry[i].addr & 0xFFFFFFFC) == (addr & 0xFFFFFFFC)) {
      uint32_t wdata = entry[i].data;
      uint32_t waddr = entry[i].addr;
      uint32_t wstrb;
      if (entry[i].size == 0b00)
        wstrb = 0b1;
      else if (entry[i].size == 0b01)
        wstrb = 0b11;
      else
        wstrb = 0b1111;

      int offset = entry[i].addr & 0x3;
      wstrb = wstrb << offset;
      wdata = wdata << (offset * 8);

      uint32_t mask = 0;
      if (wstrb & 0b1)
        mask |= 0xFF;
      if (wstrb & 0b10)
        mask |= 0xFF00;
      if (wstrb & 0b100)
        mask |= 0xFF0000;
      if (wstrb & 0b1000)
        mask |= 0xFF000000;

      data = (mask & wdata) | (~mask & data);
    }
    LOOP_INC(i, STQ_NUM);
    count--;
  }

  int idx = back.rob.deq_ptr << 2;

  while (idx != rob_idx) {
    int line_idx = idx >> 2;
    int bank_idx = idx & 0b11;
    if (back.rob.entry[bank_idx][line_idx].valid &&
        is_store(back.rob.entry[bank_idx][line_idx].uop)) {
      int stq_idx = back.rob.entry[bank_idx][line_idx].uop.stq_idx;
      if (entry[stq_idx].valid &&
          (!entry[stq_idx].data_valid || !entry[stq_idx].addr_valid)) {
        // 有未准备好的store，停止转发
        stall_load = true;
        return;
      }
      if ((entry[stq_idx].addr & 0xFFFFFFFC) == (addr & 0xFFFFFFFC)) {
        uint32_t wdata = entry[stq_idx].data;
        uint32_t waddr = entry[stq_idx].addr;
        uint32_t wstrb;
        if (entry[stq_idx].size == 0b00)
          wstrb = 0b1;
        else if (entry[stq_idx].size == 0b01)
          wstrb = 0b11;
        else
          wstrb = 0b1111;

        int offset = entry[stq_idx].addr & 0x3;
        wstrb = wstrb << offset;
        wdata = wdata << (offset * 8);

        uint32_t mask = 0;
        if (wstrb & 0b1)
          mask |= 0xFF;
        if (wstrb & 0b10)
          mask |= 0xFF00;
        if (wstrb & 0b100)
          mask |= 0xFF0000;
        if (wstrb & 0b1000)
          mask |= 0xFF000000;

        data = (mask & wdata) | (~mask & data);
      }
    }
    LOOP_INC(idx, ROB_NUM);
  }
}
