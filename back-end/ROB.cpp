#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <iostream>

bool rob_cmp(int idx1, bool bit1, int idx2, bool bit2) {
  bool ret;
  if (bit1 == bit2) {
    ret = (idx1 < idx2);
  } else {
    ret = (idx1 > idx2);
  }

  return ret;
}

void ROB::comb() {
  out.enq_idx = enq_ptr;
  out.enq_bit = entry[0].pos_bit;

  int ld_commit_num = 0;
  bool store = false;
  int i = deq_ptr;
  int num = 0;
  while (entry[i].complete && num < ISSUE_WAY) {
    if ((entry[i].op == SW || entry[i].op == SH || entry[i].op == SB)) {

      if (store)
        break;
      else {
        store = true;
      }
    }

    out.commit_entry[num] = entry[deq_ptr];
    num++;

    if (entry[i].op == LW || entry[i].op == LB || entry[i].op == LH ||
        entry[i].op == LHU || entry[i].op == LBU)
      ld_commit_num++;

    i = (i + 1) % ROB_NUM;
  }

  while (num < ISSUE_WAY) {
    out.commit_entry[num].op = NONE;
    num++;
  }

  out.ld_commit_num = ld_commit_num;
}

void ROB::seq() {

  // 3. retire
  bool store = false;
  int i = 0;

  while (entry[deq_ptr].complete && i < ISSUE_WAY) {
    if ((entry[deq_ptr].op == SW || entry[deq_ptr].op == SH ||
         entry[deq_ptr].op == SB)) {

      if (store)
        break;
      else {
        /*cvt_number_to_bit(output_data, entry[deq_ptr].store_addr);*/
        /*POS_OUT_STORE_ADDR = entry[deq_ptr].store_addr;*/
      }
    }

#ifdef CONFIG_DIFFTEST
    back.difftest(entry[deq_ptr].PC + 4);
#endif
    i++;
    entry[deq_ptr].complete = false;
    entry[deq_ptr].branch = false;
    entry[deq_ptr].op = NONE;
    deq_ptr = (deq_ptr + 1) % ROB_NUM;
    if (deq_ptr == 0)
      pos_invert();
    count--;

    if (log) {
      cout << "ROB commit PC 0x" << hex << entry[deq_ptr].PC << endl;
      /*rename.print_reg();*/
      /*rename.print_RAT();*/
    }
  }

  // 1 dispatch进入rob
  // 处理rob满的情况
  /*if (ROB_NUM - count < WAY) {*/
  /*  out.full = true;*/
  /*  return;*/
  /*}*/

  out.full = false;

  for (int i = 0; i < INST_WAY; i++) {
    if (in.op[i] != NONE) {
      entry[enq_ptr].PC = in.PC[i];
      entry[enq_ptr].op = in.op[i];
      entry[enq_ptr].dest_preg_idx = in.dest_preg_idx[i];
      entry[enq_ptr].dest_areg_idx = in.dest_areg_idx[i];
      entry[enq_ptr].dest_en = in.dest_en[i];
      entry[enq_ptr].old_dest_preg_idx = in.old_dest_preg_idx[i];
      entry[enq_ptr].complete = false;
      enq_ptr = (enq_ptr + 1) % ROB_NUM;
      if (enq_ptr == 0)
        pos_invert();
      count++;
    }
  }

  // 2. execute
  for (int i = 0; i < ALU_NUM; i++) {
    if (in.complete[i] == false)
      continue;

    entry[in.idx[i]].complete = true;
    if (in.br_taken[i]) {
      entry[in.idx[i]].branch = true;
    }
  }

  for (int i = 0; i < AGU_NUM; i++) {
    int j = i + ALU_NUM;
    if (in.complete[j] == false)
      continue;

    entry[in.idx[j]].complete = true;

    if (entry[in.idx[j]].op == SW || entry[in.idx[j]].op == SH ||
        entry[in.idx[j]].op == SB) {
      entry[in.idx[j]].store_addr = in.store_addr[i];
      entry[in.idx[j]].store_data = in.store_data[i];
      /*entry[in.idx[j]].store_size = in.store_size[i];*/
    }
  }
}

void ROB::init() {

  for (int i = 0; i < ROB_NUM; i++) {
    entry[i].op = NONE;
    entry[i].pos_bit = 0;
    entry[i].complete = false;
  }
  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}

void ROB::pos_invert() {

  for (int i = 0; i < ROB_NUM; i++) {
    entry[i].pos_bit = !entry[i].pos_bit;
  }
}

/*uint32_t ROB::get_pc(int idx) { return entry[idx].PC; }*/
/**/
/*void ROB::complete(int idx) { entry[idx].complete = true; }*/
/*void ROB::branch(int idx) { entry[idx].branch = true; }*/

/*ROB_entry ROB::commit() {*/
/*  ROB_entry ret = entry[deq_ptr];*/
/*  if (ret.complete) {*/
/*    entry[deq_ptr].complete = false;*/
/*    entry[deq_ptr].branch = false;*/
/*    entry[deq_ptr].op = NONE;*/
/*    deq_ptr = (deq_ptr + 1) % ROB_NUM;*/
/*    if (deq_ptr == 0)*/
/*      pos_invert();*/
/*    count--;*/
/*  }*/
/**/
/*  return ret;*/
/*}*/
/**/
/*void ROB::store(int idx, uint32_t address, uint32_t data) {*/
/**/
/*  entry[idx].store_addr = address;*/
/*  entry[idx].store_data = data;*/
/*}*/
/**/
/*bool ROB::check_raw(int idx) {*/
/*  for (int i = 0; i < ROB_NUM; i++) {*/
/*    if (entry[i].op != NONE && entry[i].dest_en &&*/
/*        entry[i].dest_preg_idx == idx && !entry[i].complete)*/
/*      return true;*/
/*  }*/
/**/
/*  return false;*/
/*}*/
