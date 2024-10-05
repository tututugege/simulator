#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>

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
    valid_1[idx] = false;

    commit_num++;
  }

  deq_ptr_1 = (deq_ptr + commit_num) % ROB_NUM;

  // dispatch进入rob
  bool full = (count_1 == ROB_NUM);
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i] && !(in.br_taken && in.tag[i] == in.br_tag) && full) {
      ROB_entry enq_entry = {in.PC[i],
                             in.op[i],
                             in.dest_preg_idx[i],
                             in.dest_areg_idx[i],
                             in.old_dest_preg_idx[i],
                             in.dest_en[i]};
      entry.to_sram.waddr[i] = enq_ptr_1;
      entry.to_sram.we[i] = true;
      entry.to_sram.wdata[i] = enq_entry;
      valid_1[enq_ptr_1] = true;
      tag_1[enq_ptr_1] = in.tag[i];
      enq_ptr_1 = (enq_ptr_1 + 1) % ROB_NUM;
      if (enq_ptr_1 == 0)
        pos_bit_1 = !pos_bit;
      count_1++;
      full = (count_1 == ROB_NUM);
      out.ready[i] = true;
    } else {
      entry.to_sram.we[i] = false;
      out.ready[i] = false;
    }
  }

  //  执行完毕的标记
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.complete[i] == false)
      continue;

    complete_1[in.idx[i]] = true;
  }

  // 分支清空指令
  if (in.br_taken) {
    for (int i = 0; i < ROB_NUM; i++) {
      if (tag[i] == in.br_tag && valid[i]) {
        valid_1[in.idx[i]] = false;
        complete_1[in.idx[i]] = false;
      }
    }
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
    complete[i] = complete_1[i];
    valid[i] = valid_1[i];
    tag[i] = tag_1[i];
    /*trap[i] = trap_1[i];*/
  }

  pos_bit = pos_bit_1;
  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  count = count_1;
  out.enq_idx = enq_ptr;
  out.enq_bit = pos_bit;
}

void ROB::init() {

  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}

/*uint32_t ROB::get_pc(int idx) { return entry[idx].PC; }*/
/**/
/*void ROB::complete(int idx) { entry[idx].complete = true; }*/
/*void ROB::branch(int idx) { entry[idx].branch = true; }*/
