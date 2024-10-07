#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <util.h>

void ROB::comb() {
  // 提交指令
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
    complete_1[idx] = false;
    valid_1[idx] = false;
    if (out.valid[complete_num] == false)
      break;
  }
  for (int i = complete_num; i < ISSUE_WAY; i++) {
    out.valid[i] = false;
  }

  deq_ptr_1 = (deq_ptr + complete_num) % ROB_NUM;
  count_1 = count - complete_num;

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
  out.to_ren_all_ready = andR(out.to_ren_ready, INST_WAY);

  for (int i = 0; i < INST_WAY; i++) {
    if (in.from_ren_valid[i] && out.to_ren_ready[i]) {
      Inst_info enq_entry = in.inst[i];
      entry.to_sram.waddr[i] = enq_ptr_1;
      entry.to_sram.we[i] = true;
      entry.to_sram.wdata[i] = enq_entry;
      valid_1[enq_ptr_1] = true;
      tag_1[enq_ptr_1] = in.inst[i].tag;
      enq_ptr_1 = (enq_ptr_1 + 1) % ROB_NUM;
    } else {
      entry.to_sram.we[i] = false;
    }
  }

  //  执行完毕的标记
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    out.to_ex_ready[i] = true;
    if (in.from_ex_valid[i] && out.to_ex_all_ready) {
      complete_1[in.rob_idx[i]] = true;
    }
  }
  out.to_ex_all_ready = true;
  out.enq_idx = enq_ptr;
}

void ROB::seq() {

  if (LOG) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (out.valid[i]) {
#ifdef CONFIG_DIFFTEST
        back.arch_update(out.commit_entry[i].dest_preg,
                         out.commit_entry[i].dest_areg);
        back.difftest(out.commit_entry[i].pc_next);
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
