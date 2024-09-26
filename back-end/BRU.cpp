#include "config.h"
#include <BRU.h>
#include <cstdint>

void BRU::cycle() {
  uint32_t pc_br = in.pc + in.off;

  switch (in.op) {
  case BR:
    out.br_taken = in.alu_out;
    break;
  case JAL:
    out.br_taken = true;
    break;
  case JALR:
    out.br_taken = true;
    pc_br = (in.src1 + in.off) & (~0x1);
    break;
  default:
    out.br_taken = false;
  }

  if (out.br_taken)
    out.pc_next = pc_br;
  else
    out.pc_next = in.pc + 4;
}

void Br_Tag::init() {
  for (int i = TAG_NUM - 1; i >= 0; i--) {
    free_tag_list.to_fifo.we[0] = true;
    free_tag_list.to_fifo.wdata[0] = i;
    free_tag_list.write();
  }
}

void Br_Tag::comb() {

  // 分配新tag
  for (int i = 0; i < INST_WAY; i++) {
    free_tag_list.to_fifo.re[i] = in.alloc[i];
  }

  // 释放tag
  for (int i = 0; i < INST_WAY; i++) {
    free_tag_list.to_fifo.we[i] = in.free[i];
    free_tag_list.to_fifo.wdata[i] = in.free_tag[i];
  }

  tag_list.read();

  for (int i = 0; i < INST_WAY; i++) {
    // 分配新tag

    // 新分配的tag写入tag_list
    tag_list.to_fifo.we[i] = in.alloc[i];
    tag_list.to_fifo.wdata[i] = tag_list.from_fifo.rdata[i];
  }

  // 释放tag
  for (int i = 0; i < INST_WAY; i++) {
    free_tag_list.to_fifo.we[i] = in.free[i];
    free_tag_list.to_fifo.wdata[i] = in.free_tag[i];
  }

  uint32_t last_tag = now_tag;
  for (int i = 0; i < INST_WAY; i++) {
    if (i == 0 || in.alloc[i - 1] == 0) {
      out.now_tag[i] = last_tag;
    } else {
      out.now_tag[i] = tag_list.from_fifo.rdata[i - 1];
      last_tag = out.now_tag[i];
    }
  }
}

void Br_Tag::seq() {
  tag_list.write();
  free_tag_list.write();
}
