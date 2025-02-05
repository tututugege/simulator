#include "TOP.h"
#include <STQ.h>
#include <config.h>
#include <util.h>

extern Back_Top back;

enum STATE { IDLE, WAIT };
void STQ::comb() {
  back.out.bready = true;
  static int state;

  // 写端口 同时给ld_IQ发送唤醒信息
  if (entry[deq_ptr].valid && entry[deq_ptr].compelete) {
    if (state == IDLE) {
    } else if (state == WAIT) {
      if (back.in.bvalid && back.out.bready) {
        entry[deq_ptr].valid = false;
        entry[deq_ptr].compelete = false;
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
      }
    }
  } else {
    back.out.wvalid = false;
  }

  int num = count;
  int idx = enq_ptr;
  for (int i = 0; i < INST_WAY; i++) {
    if (!io.ren2stq->valid[i]) {
      io.stq2ren->ready[i] = true;
    } else {
      if (num < STQ_NUM) {
        io.stq2ren->ready[i] = true;
        io.stq2ren->stq_idx[i] = idx;
        num++;
        idx = (idx + 1) % STQ_NUM;
      } else {
        io.stq2ren->ready[i] = false;
      }
    }
  }

  // 指令store依赖信息
  for (int i = 0; i < STQ_NUM; i++) {
    io.stq2ren->stq_valid[i] = entry[i].valid;
  }

  // 分支清空
  /*if (in.br.mispred) {*/
  /*  for (int i = 0; i < STQ_NUM; i++) {*/
  /*    if (entry[i].valid && in.br.br_mask[entry[i].tag]) {*/
  /*      entry_1[i].valid = false;*/
  /*      count_1--;*/
  /*      LOOP_DEC(enq_ptr_1, STQ_NUM);*/
  /*    }*/
  /*  }*/
  /*}*/
}

void STQ::seq() {

  // 入队
  for (int i = 0; i < INST_WAY; i++) {
    if (io.ren2stq->dis_fire[i]) {
      entry[enq_ptr].tag = io.ren2stq->tag[i];
      entry[enq_ptr].valid = true;
      count++;
      LOOP_INC(enq_ptr, STQ_NUM);
    }
  }

  // 地址数据写入 若项无效说明被br清除
  if (io.exe2stq->wr_valid && entry[io.exe2stq->wr_idx].valid) {
    entry[io.exe2stq->wr_idx] = io.exe2stq->write;
  }

  // commit标记为可执行
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.rob_commit->commit_entry[i].valid) {
      entry[commit_ptr].compelete = true;
      commit_ptr = (commit_ptr + 1) % STQ_NUM;
    }
  }
}
