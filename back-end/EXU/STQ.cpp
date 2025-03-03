#include "TOP.h"
#include <STQ.h>
#include <config.h>
#include <util.h>

extern Back_Top back;

enum STATE { IDLE, WAIT };
void STQ::comb() {
  back.out.bready = true;
  back.out.wvalid = false;

  static int state;
  for (int i = 0; i < STQ_NUM; i++) {
    io.stq2iss->valid[i] = false;
  }

  int num = count;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (!io.ren2stq->valid[i]) {
      io.stq2ren->ready[i] = true;
    } else {
      if (num < STQ_NUM) {
        io.stq2ren->ready[i] = true;
        num++;
      } else {
        io.stq2ren->ready[i] = false;
      }
    }
  }

  // 写端口 同时给ld_IQ发送唤醒信息
  if (entry[deq_ptr].valid && entry[deq_ptr].compelete) {
    if (state == IDLE) {
      back.out.wvalid = true;
      back.out.wdata = entry[deq_ptr].data;
      back.out.waddr = entry[deq_ptr].addr;
      if (entry[deq_ptr].size == 0b00)
        back.out.wstrb = 0b1;
      else if (entry[deq_ptr].size == 0b01)
        back.out.wstrb = 0b11;
      else
        back.out.wstrb = 0b1111;

      int offset = entry[deq_ptr].addr & 0x3;
      back.out.wstrb = back.out.wstrb << offset;
      back.out.wdata = back.out.wdata << (offset * 8);

      if (back.out.wvalid && back.in.wready) {
        state = WAIT;
      }
    } else if (state == WAIT) {
      if (back.in.bvalid && back.out.bready) {
        entry[deq_ptr].valid = false;
        entry[deq_ptr].compelete = false;
        io.stq2iss->valid[deq_ptr] = true;
        LOOP_INC(deq_ptr, STQ_NUM);
        count--;
        state = IDLE;
      }
    } else {
      back.out.wvalid = false;
    }
  }

  // 指令store依赖信息
  for (int i = 0; i < STQ_NUM; i++) {
    io.stq2ren->stq_valid[i] = entry[i].valid;
  }
}

void STQ::seq() {

  // 入队
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2stq->dis_fire[i] && io.ren2stq->valid[i]) {
      entry[enq_ptr].tag = io.ren2stq->tag[i];
      entry[enq_ptr].valid = true;
      count++;
      LOOP_INC(enq_ptr, STQ_NUM);
    }
  }

  // 地址数据写入 若项无效说明被br清除
  Inst_info *inst = &io.exe2stq->entry.inst;
  int idx = inst->stq_idx;
  if (io.exe2stq->entry.valid && entry[idx].valid) {
    entry[idx].data = inst->src2_rdata;
    entry[idx].addr = inst->result;
    entry[idx].size = inst->func3;
  }

  // commit标记为可执行
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.rob_commit->commit_entry[i].valid &&
        io.rob_commit->commit_entry[i].inst.op == STORE) {
      entry[commit_ptr].compelete = true;
      LOOP_INC(commit_ptr, STQ_NUM);
    }
  }

  // 分支清空
  if (io.id_bc->mispred) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && !entry[i].compelete &&
          (io.id_bc->br_mask & (1 << entry[i].tag))) {
        entry[i].valid = false;
        count--;
        LOOP_DEC(enq_ptr, STQ_NUM);
      }
    }
  }

  io.stq2ren->stq_idx = enq_ptr;
}
