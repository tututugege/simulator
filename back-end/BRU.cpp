#include <BRU.h>
#include <config.h>

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
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
  }
}

void Br_Tag::comb() {
  // 分配新tag
  int free_tag_num = 0;
  int free_tag[INST_WAY];
  for (int i = 0; i < MAX_BR_NUM && free_tag_num < INST_WAY; i++) {
    if (tag_vec[i])
      free_tag[free_tag_num++] = i;
  }

  int tag_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i])
      out.ready[i] = true;
    else if (tag_num < free_tag_num) {
      out.tag[i] = last_tag_1;
      tag_fifo_1[enq_ptr_1] = free_tag[tag_num];
      last_tag_1 = free_tag[tag_num];
      enq_ptr_1 = (enq_ptr_1 + 1) % (MAX_BR_NUM - 1);
      out.ready[i] = true;
      tag_num++;
    } else
      out.ready[i] = false;
  }

  // 释放tag
  for (int i = 0; i < MAX_BR_NUM - 1; i++) {
    if (in.free_valid[i]) {
      tag_vec[in.free_tag[i]] = true;
      deq_ptr_1 = (deq_ptr_1 + 1) % (MAX_BR_NUM - 1);
    }
  }
}

void Br_Tag::seq() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    tag_fifo[i] = tag_fifo_1[i];
  }
  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  last_tag = last_tag_1;
}
