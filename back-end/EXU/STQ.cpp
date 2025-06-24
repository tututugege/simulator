#include "TOP.h"
#include "frontend.h"
#include <STQ.h>
#include <config.h>
#include <cstdint>
#include <iostream>
#include <util.h>

extern Back_Top back;
uint32_t amo(uint32_t src1, uint32_t src2, AMO_op amoop);

enum STATE { IDLE, WAIT };
void STQ::comb() {
  /*back.out.bready = true;*/
  /*back.out.wvalid = false;*/

  static int state;
  for (int i = 0; i < STQ_NUM; i++) {
    io.stq2iss->valid[i] = false;
  }

  int num = count;

  for (int i = 0; i < DECODE_WIDTH; i++) {
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
  if (entry[deq_ptr].valid && entry[deq_ptr].complete) {
    /*if (state == IDLE) {*/
    /*  back.out.wvalid = true;*/
    /*  back.out.wdata = entry[deq_ptr].data;*/
    /*  back.out.waddr = entry[deq_ptr].addr;*/
    /*  if (entry[deq_ptr].size == 0b00)*/
    /*    back.out.wstrb = 0b1;*/
    /*  else if (entry[deq_ptr].size == 0b01)*/
    /*    back.out.wstrb = 0b11;*/
    /*  else*/
    /*    back.out.wstrb = 0b1111;*/
    /**/
    /*  int offset = entry[deq_ptr].addr & 0x3;*/
    /*  back.out.wstrb = back.out.wstrb << offset;*/
    /*  back.out.wdata = back.out.wdata << (offset * 8);*/
    /**/
    /*  if (back.out.wvalid && back.in.wready) {*/
    /*    state = WAIT;*/
    /*  }*/
    /*} else if (state == WAIT) {*/
    /*  if (back.in.bvalid && back.out.bready) {*/
    /*    entry[deq_ptr].valid = false;*/
    /*    entry[deq_ptr].complete = false;*/
    /*    io.stq2iss->valid[deq_ptr] = true;*/
    /*    LOOP_INC(deq_ptr, STQ_NUM);*/
    /*    count--;*/
    /*    state = IDLE;*/
    /*  }*/
    /*} else {*/
    /*  back.out.wvalid = false;*/
    /*}*/
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

    p_memory[waddr / 4] = (mask & wdata) | (~mask & old_data);

    if (waddr == UART_BASE) {
      char temp;
      temp = wdata & 0x000000ff;
      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] & 0xffffff00;
      cout << temp;
    }

    if (waddr == 0x10000001 && (wdata & 0x000000ff) == 7) {
      // cerr << "UART enabled!" << endl;
      /*output_data_from_RISCV[1152 + 31 - 9] = 1; // mip*/
      /*output_data_from_RISCV[1568 + 31 - 9] = 1; // sip*/
      p_memory[0xc201004 / 4] = 0xa;
      // log = true;
      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] & 0xfff0ffff;
    }
    if (waddr == 0x10000001 && (wdata & 0x000000ff) == 5) {
      // cerr << "UART disabled2!" << endl;
      //  ref_memory[0xc201004/4] = 0x0;
      p_memory[0x10000000 / 4] =
          p_memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
    }
    if (waddr == 0xc201004 && (wdata & 0x000000ff) == 0xa) {
      // cerr << "UART disabled1!" << endl;
      p_memory[0xc201004 / 4] = 0x0;
      /*output_data_from_RISCV[1152 + 31 - 9] = 0; // mip*/
      /*output_data_from_RISCV[1568 + 31 - 9] = 0; // sip*/
    }

    /*extern int sim_time;*/
    if (MEM_LOG) {
      cout << "store data " << hex << ((mask & wdata) | (~mask & old_data))
           << " in " << (waddr & 0xFFFFFFFC) << endl;
    }

    entry[deq_ptr].valid = false;
    entry[deq_ptr].complete = false;
    io.stq2iss->valid[deq_ptr] = true;
    LOOP_INC(deq_ptr, STQ_NUM);
    count--;
  }

  // 指令store依赖信息
  for (int i = 0; i < STQ_NUM; i++) {
    io.stq2ren->stq_valid[i] = entry[i].valid;
  }
}

void STQ::seq() {

  // 入队
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2stq->dis_fire[i] && io.ren2stq->valid[i]) {
      entry[enq_ptr].tag = io.ren2stq->tag[i];
      entry[enq_ptr].valid = true;
      entry[enq_ptr].amo_data1_valid = false;
      entry[enq_ptr].amo_data2_valid = false;
      count++;
      LOOP_INC(enq_ptr, STQ_NUM);
    }
  }

  // 地址数据写入 若项无效说明被br清除
  Inst_uop *inst = &io.exe2stq->entry.uop;
  int idx = inst->stq_idx;
  if (io.exe2stq->entry.valid && entry[idx].valid) {
    entry[idx].addr = inst->result;
    entry[idx].size = inst->func3;

    if (!entry[idx].amo_data1_valid) {
      entry[idx].data = inst->src2_rdata;
      entry[idx].amo_data2_valid = true;
    } else {
      entry[idx].data = amo(entry[idx].data, inst->src2_rdata, inst->amoop);
    }
  }

  // AMO指令处理
  idx = io.prf2stq->stq_idx;
  if (io.prf2stq->valid) {
    if (!entry[idx].amo_data2_valid) {
      entry[idx].amo_data1_valid = true;
      entry[idx].data = io.prf2stq->load_data;
    } else {
      entry[idx].data =
          amo(io.prf2stq->load_data, entry[idx].data, io.prf2stq->amoop);
    }
  }

  // commit标记为可执行
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.rob_commit->commit_entry[i].valid &&
        is_store(io.rob_commit->commit_entry[i].uop.op) &&
        !io.rob_commit->commit_entry[i].uop.page_fault_store) {
      entry[commit_ptr].complete = true;
      LOOP_INC(commit_ptr, STQ_NUM);
    }
  }

  // 分支清空
  if (io.dec_bcast->mispred) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && !entry[i].complete &&
          (io.dec_bcast->br_mask & (1 << entry[i].tag))) {
        entry[i].valid = false;
        entry[i].complete = false;
        count--;
        LOOP_DEC(enq_ptr, STQ_NUM);
      }
    }
  }

  if (io.rob_bc->flush) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && !entry[i].complete) {
        entry[i].valid = false;
        entry[i].complete = false;
        count--;
        LOOP_DEC(enq_ptr, STQ_NUM);
      }
    }
  }

  io.stq2ren->stq_idx = enq_ptr;
}

uint32_t amo(uint32_t src1, uint32_t src2, AMO_op amoop) {
  uint32_t res;
  switch (amoop) {
  case AMOADD: { // amoadd.w
    res = src1 + src2;
    break;
  }
  case AMOSWAP: { // amoswap.w
    res = src2;
    break;
  }
  case AMOXOR: { // amoxor.w
    res = src1 ^ src2;
    break;
  }
  case AMOOR: { // amoor.w
    res = src1 | src2;
    break;
  }
  case AMOAND: { // amoand.w
    res = src1 & src2;
    break;
  }
  case AMOMIN: { // amomin.w
    res = ((int)src1 > (int)src2) ? src2 : src1;
    break;
  }
  case AMOMAX: { // amomax.w
    res = ((int)src1 > (int)src2) ? src1 : src2;
    break;
  }
  case AMOMINU: { // amominu.w
    res = ((uint32_t)src1 > (uint32_t)src2) ? src2 : src1;
    break;
  }
  case AMOMAXU: { // amomaxu.w
    res = ((uint32_t)src1 > (uint32_t)src2) ? src1 : src2;
    break;
  }
  default:
    res = src2;
    break;
  }

  return res;
}
