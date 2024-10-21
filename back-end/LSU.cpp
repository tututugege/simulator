#include <LSU.h>
#include <config.h>
#include <util.h>

void STQ::comb_deq() {
  // 写端口 同时给ld_IQ发送唤醒信息
  if (entry[deq_ptr].valid && entry[deq_ptr].compelete) {
    entry_1[deq_ptr].valid = false;
    entry_1[deq_ptr].compelete = false;
    out.wen = true;
    out.wdata = entry[deq_ptr].data;
    out.waddr = entry[deq_ptr].addr;
    /*out.wstrb = entry[deq_ptr].size;*/
    deq_ptr_1 = (deq_ptr + 1) % STQ_NUM;
    count_1--;
  } else {
    out.wen = false;
  }
}

void STQ::comb_alloc() {
  int num = count;
  int idx = enq_ptr;
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i]) {
      out.ready[i] = true;
    } else {
      if (num < STQ_NUM) {
        out.ready[i] = true;
        out.enq_idx[i] = idx;
        num++;
        idx = (idx + 1) % STQ_NUM;
      } else {
        out.ready[i] = false;
      }
    }
  }
  // 指令store依赖信息
  for (int i = 0; i < STQ_NUM; i++) {
    out.entry_valid[i] = entry_1[i].valid;
  }

  // 分支清空
  if (in.br.br_taken) {
    for (int i = 0; i < STQ_NUM; i++) {
      if (entry[i].valid && in.br.br_mask[entry[i].tag]) {
        entry_1[i].valid = false;
        count_1--;
        LOOP_DEC(enq_ptr_1, STQ_NUM);
      }
    }
  }
}

void STQ::comb_fire() {
  // 入队
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      entry_1[enq_ptr_1].tag = in.tag[i];
      entry_1[enq_ptr_1].valid = true;
      count_1 = count_1 + 1;
      LOOP_INC(enq_ptr_1, STQ_NUM);
    }
  }

  // 地址数据写入 若项无效说明被br清除
  if (in.wr_valid && entry_1[in.wr_idx].valid) {
    entry_1[in.wr_idx] = in.write;
  }

  // commit标记为可执行
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit[i]) {
      entry_1[commit_ptr_1].compelete = true;
      commit_ptr_1 = (commit_ptr_1 + 1) % STQ_NUM;
    }
  }
}

void STQ::seq() {
  for (int i = 0; i < STQ_NUM; i++) {
    entry[i] = entry_1[i];
  }
  deq_ptr = deq_ptr_1;
  enq_ptr = enq_ptr_1;
  commit_ptr = commit_ptr_1;
  count = count_1;
}
