#include "config.h"
#include <BRU.h>

void BRU::cycle() {
  uint32_t pc_br = in.pc + in.off;

  switch (in.op) {
  case BEQ:
    out.br_taken = (in.src1 == in.src2);
    break;
  case BNE:
    out.br_taken = (in.src1 != in.src2);
    break;
  case BGE:
    out.br_taken = ((signed)in.src1 >= (signed)in.src2);
    break;
  case BLT:
    out.br_taken = ((signed)in.src1 < (signed)in.src2);
    break;
  case BGEU:
    out.br_taken = ((unsigned)in.src1 > (unsigned)in.src2);
    break;
  case BLTU:
    out.br_taken = ((unsigned)in.src1 >= (unsigned)in.src2);
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

void Br_Tag::comb() {
  for (int i = 0; i < INST_WAY; i++) {
    free_tag_list.to_fifo.re[i] = in.alloc[i];
    free_tag_list.to_fifo.we[i] = in.alloc[i];
  }

  tag_list.read();

  for (int i = 0; i < INST_WAY; i++) {
    out.alloc_tag[i] = tag_list.from_fifo.rdata[i];
    tag_list.to_fifo.we[i] = in.alloc[i];
    tag_list.to_fifo.wdata[i] = out.alloc_tag[i];
  }

  for (int i = 0; i < INST_WAY; i++) {
    free_tag_list.to_fifo.we[i] = in.free[i];
    free_tag_list.to_fifo.wdata[i] = in.free_tag[i];
  }
}

void Br_Tag::seq() {
  tag_list.write();
  free_tag_list.write();
}
