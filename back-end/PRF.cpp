#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <iostream>

// TODO ADDR
#define TAG_WIDTH 20
#define INDEX_WIDTH 4
#define WORD_WIDTH 4
#define OFFSET_WIDTH 4

void PRF::comb() {
  // LSU
  io.prf2lsu->valid = false;
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid &&
        (inst_r[i].inst.op == STORE || inst_r[i].inst.op == LOAD)) {
      io.prf2lsu->lsq_entry = inst_r[i].inst.lsu_idx;
      io.prf2lsu->valid = true;
      io.prf2lsu->op = (inst_r[i].inst.op == STORE) ? OP_ST : OP_LD;

      io.prf2lsu->vtag =
          inst_r[i].inst.result >> (INDEX_WIDTH + WORD_WIDTH + OFFSET_WIDTH);
      io.prf2lsu->index =
          ((inst_r[i].inst.result >> (WORD_WIDTH + OFFSET_WIDTH)) &
           ((1 << INDEX_WIDTH) - 1));

      io.prf2lsu->word =
          ((inst_r[i].inst.result >> (OFFSET_WIDTH)) & ((1 << WORD_WIDTH) - 1));

      io.prf2lsu->offset = ((inst_r[i].inst.result) & (1 << OFFSET_WIDTH) - 1);

      for (int i = 0; i < 4; i++) {
        io.prf2lsu->wdata_b4_shf[i] =
            ((inst_r[i].inst.src2_rdata) >> (8 * i)) & (0xFF);
      }
    } else {
      io.prf2exe->ready[i] = true;
    }
  }

  // 根据分支结果向前端返回信息
  // TODO::magic_number
  io.prf2id->mispred = false;
  if (inst_r[4].valid && is_branch(inst_r[4].inst.op) &&
      inst_r[4].inst.mispred) {

    io.prf2id->mispred = true;
    io.prf2id->redirect_pc = inst_r[4].inst.pc_next;
    io.prf2id->br_tag = inst_r[4].inst.tag;

    if (LOG)
      cout << "misprediction redirect_pc 0x" << hex << io.prf2id->redirect_pc
           << endl;
  }

  // bypass
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.prf2exe->iss_entry[i] = io.iss2prf->iss_entry[i];
    Inst_entry *entry = &io.prf2exe->iss_entry[i];

    if (entry->valid) {
      if (entry->inst.src1_en) {
        entry->inst.src1_rdata = reg_file[entry->inst.src1_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].inst.dest_en &&
              inst_r[j].inst.dest_preg == entry->inst.src1_preg)
            entry->inst.src1_rdata = inst_r[j].inst.result;
        }

        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].inst.dest_en &&
              io.exe2prf->entry[j].inst.dest_preg == entry->inst.src1_preg)
            entry->inst.src1_rdata = io.exe2prf->entry[j].inst.result;
        }
      }

      if (entry->inst.src2_en) {
        entry->inst.src2_rdata = reg_file[entry->inst.src2_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].inst.dest_en &&
              inst_r[j].inst.dest_preg == entry->inst.src2_preg)
            entry->inst.src2_rdata = inst_r[j].inst.result;
        }

        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].inst.dest_en &&
              io.exe2prf->entry[j].inst.dest_preg == entry->inst.src2_preg)
            entry->inst.src2_rdata = io.exe2prf->entry[j].inst.result;
        }
      }
    }
  }

  // 向ROB发送完成信息
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      io.prf2rob->entry[i] = inst_r[i];
    else
      io.prf2rob->entry[i].valid = false;
  }

  /*// TODO: Magic number*/
  /*if (inst_r[4].valid && inst_r[4].inst.dest_en) {*/
  /*  io.prf_awake->wake.valid = true;*/
  /*  io.prf_awake->wake.preg = inst_r[4].inst.dest_preg;*/
  /*} else {*/
  /*  io.prf_awake->wake.valid = false;*/
  /*}*/
}

void PRF::seq() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid && inst_r[i].inst.dest_en &&
        inst_r[i].inst.op != LOAD) {
      reg_file[inst_r[i].inst.dest_preg] = inst_r[i].inst.result;
    }

    if (inst_r[i].valid &&
        (inst_r[i].inst.op == LOAD || inst_r[i].inst.op == STORE)) {
      io.exe2prf->ready[i] = io.lsu2prf->ready;
    }

    if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r[i] = io.exe2prf->entry[i];
    } else {
      inst_r[i].valid = false;
    }
  }

  if (io.id_bc->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid && (io.id_bc->br_mask & (1 << inst_r[i].inst.tag))) {
        inst_r[i].valid = false;
      }
    }
  }
}
