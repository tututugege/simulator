#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <string.h>

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
  /**/
  /*int ld_commit_num = 0;*/
  /*bool store = false;*/
  /*int i = deq_ptr;*/
  /*int num = 0;*/
  /*while (entry[i].complete && num < ISSUE_WAY) {*/
  /*  if ((entry[i].op == SW || entry[i].op == SH || entry[i].op == SB)) {*/
  /**/
  /*    if (store)*/
  /*      break;*/
  /*    else {*/
  /*      store = true;*/
  /*    }*/
  /*  }*/
  /**/
  /*  out.commit_entry[num] = entry[deq_ptr];*/
  /*  num++;*/
  /**/
  /*  if (entry[i].op == LW || entry[i].op == LB || entry[i].op == LH ||*/
  /*      entry[i].op == LHU || entry[i].op == LBU)*/
  /*    ld_commit_num++;*/
  /**/
  /*  i = (i + 1) % ROB_NUM;*/
  /*}*/
  /**/
  /*while (num < ISSUE_WAY) {*/
  /*  out.commit_entry[num].op = NONE;*/
  /*  num++;*/
  /*}*/
  /**/
  /*out.ld_commit_num = ld_commit_num;*/

  // 读取提交的指令
  for (int i = 0; i < ISSUE_WAY; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    entry.to_sram.raddr[i] = idx;
  }
  entry.read();

  int complete_num;
  for (complete_num = 0; complete_num < ISSUE_WAY; complete_num++) {
    int idx = (deq_ptr + complete_num) % ROB_NUM;
    out.commit_entry[complete_num] = entry.from_sram.rdata[complete_num];
    out.valid[complete_num] = valid[idx] && complete[idx];
    if (out.valid[complete_num] == false)
      break;
  }

  for (; complete_num < ISSUE_WAY; complete_num++) {
    out.valid[complete_num] = false;
  }

  // 提交指令
  int commit_num = 0;
  for (int i = 0; i < ISSUE_WAY; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    if (complete[idx] == false || valid[idx] == false)
      break;

    complete_1[idx] = false;
    branch_1[idx] = false;
    valid_1[idx] = false;

    commit_num++;
  }

  deq_ptr_1 = (deq_ptr + commit_num) % ROB_NUM;

  // dispatch进入rob
  for (int i = 0; i < INST_WAY; i++) {
    ROB_entry enq_entry = {in.PC[i],
                           in.type[i],
                           in.dest_preg_idx[i],
                           in.dest_areg_idx[i],
                           in.old_dest_preg_idx[i],
                           in.dest_en[i]};
    entry.to_sram.waddr[i] = enq_ptr_1;
    entry.to_sram.we[i] = in.valid[i];
    entry.to_sram.wdata[i] = enq_entry;
    valid_1[enq_ptr_1] = true;
    enq_ptr_1 = (enq_ptr_1 + 1) % ROB_NUM;
    if (enq_ptr_1 == 0)
      pos_bit_1 = !pos_bit;
    count++;
  }

  // 3. 执行完毕的标记
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.complete[i] == false)
      continue;

    complete_1[in.idx[i]] = true;
    branch_1[in.idx[i]] = in.br_taken[i];
  }
}

void ROB::seq() {

  if (LOG) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (out.valid[i]) {
#ifdef CONFIG_DIFFTEST
        back.arch_update(out.commit_entry[i].dest_areg_idx,
                         out.commit_entry[i].dest_preg_idx);
        back.difftest(out.commit_entry[i].PC + 4);
#endif

        cout << "ROB commit PC 0x" << hex << out.commit_entry[i].PC << endl;
      }
    }
  }

  /*rename.print_reg();*/
  /*rename.print_RAT();*/

  entry.write();
  for (int i = 0; i < ROB_NUM; i++) {
    branch[i] = branch_1[i];
    complete[i] = complete_1[i];
    trap[i] = trap_1[i];
    valid[i] = valid_1[i];
  }

  pos_bit = pos_bit_1;
  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  count = count_1;
  out.enq_idx = enq_ptr;
  out.enq_bit = pos_bit;
}

void ROB::init() {

  /*for (int i = 0; i < ROB_NUM; i++) {*/
  /*  entry[i].op = NONE;*/
  /*  entry[i].pos_bit = 0;*/
  /*  entry[i].complete = false;*/
  /*}*/
  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
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
