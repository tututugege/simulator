#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <util.h>

// 提交指令
void ROB::comb_commit() {
  int complete_num;
  for (int i = 0; i < ISSUE_WAY; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    entry.to_sram.raddr[i] = idx;
  }
  entry.read();

  for (complete_num = 0; complete_num < ISSUE_WAY; complete_num++) {
    int idx = (deq_ptr + complete_num) % ROB_NUM;
    out.commit_entry[complete_num] = entry.from_sram.rdata[complete_num];
    out.commit_entry[complete_num].pc_next = pc_next[idx];
    out.valid[complete_num] = valid[idx] && complete[idx];
    if (out.valid[complete_num]) {
      complete_1[idx] = false;
      valid_1[idx] = false;
    } else
      break;
  }

  for (int i = complete_num; i < ISSUE_WAY; i++) {
    out.valid[i] = false;
  }

  deq_ptr_1 = (deq_ptr + complete_num) % ROB_NUM;
  count_1 = count - complete_num;
}

// 生成ready
void ROB::comb_complete() {
  // dispatch进入rob
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.from_ren_valid[i])
      out.to_ren_ready[i] = true;
    else if (count_1 < ROB_NUM) {
      out.to_ren_ready[i] = true;
      count_1++;
    } else {
      out.to_ren_ready[i] = false;
    }
  }

  //  执行完毕的标记
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.from_ex_valid[i]) {
      complete_1[in.from_ex_inst[i].rob_idx] = true;
    }
  }

  if (in.br_taken) {
    int idx = (enq_ptr - 1 + ROB_NUM) % ROB_NUM;
    enq_ptr_1 = (in.br_rob_idx + 1) % ROB_NUM;
    while (idx != in.br_rob_idx) {
      valid_1[idx] = false;
      idx = (idx - 1 + ROB_NUM) % ROB_NUM;
    }
  }

  out.enq_idx = enq_ptr_1;

#ifdef CONFIG_DIFFTEST
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.from_ex_valid[i]) {
      pc_next_1[in.from_ex_inst[i].rob_idx] = in.from_ex_inst[i].pc_next;
    }
  }
#endif // DEBUG
}

void ROB::comb_enq() {
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      Inst_info enq_entry = in.from_ren_inst[i];
      entry.to_sram.waddr[i] = enq_ptr_1;
      entry.to_sram.we[i] = true;
      entry.to_sram.wdata[i] = enq_entry;
      valid_1[enq_ptr_1] = true;
      tag_1[enq_ptr_1] = in.from_ex_inst[i].tag;
      enq_ptr_1 = (enq_ptr_1 + 1) % ROB_NUM;
    } else {
      entry.to_sram.we[i] = false;
    }
  }
}

void ROB::seq() {

  if (LOG) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (out.valid[i]) {
#ifdef CONFIG_DIFFTEST
        back.difftest(out.commit_entry[i]);
#endif
        cout << "ROB commit PC 0x" << hex << out.commit_entry[i].pc << endl;
      }
    }
  }

  entry.write();
  for (int i = 0; i < ROB_NUM; i++) {
    complete[i] = complete_1[i];
    valid[i] = valid_1[i];
    tag[i] = tag_1[i];

#ifdef CONFIG_DIFFTEST
    pc_next[i] = pc_next_1[i];
#endif
  }

  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  count = count_1;
}

void ROB::init() {

  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}
