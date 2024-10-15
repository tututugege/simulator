#include <LSU.h>
#include <config.h>
/*void LDQ::init() {}*/
/*void LDQ::comb() {}*/
/**/
/*// 分配 写入 提交*/
/*void LDQ::comb_alloc() {*/
/*  // 分配*/
/*  for (int i = 0; i < INST_WAY; i++) {*/
/*    if (in.alloc[i].valid) {*/
/*      entry_1[enq_ptr] = in.alloc[i];*/
/*      enq_ptr_1 = (enq_ptr + 1) % LDQ_NUM;*/
/*      count_1++;*/
/*    }*/
/*  }*/
/**/
/*  // 写入*/
/*  if (in.write.valid) {*/
/*    for (int i = deq_ptr; i < enq_ptr; i++) {*/
/*      if (entry[i].rob_idx == in.write.rob_idx) {*/
/*        entry[i].addr = in.write.addr;*/
/*        break;*/
/*      }*/
/*    }*/
/*  }*/
/**/
/*  // 提交*/
/*  for (int i = 0; i < ISSUE_WAY; i++) {*/
/*    if (in.commit[i]) {*/
/*      entry[deq_ptr].valid = false;*/
/*      deq_ptr = (deq_ptr + 1) % LDQ_NUM;*/
/*    }*/
/*  }*/
/*}*/

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
    out.entry_valid[i] = entry[i].valid;
  }
}

void STQ::comb_fire() {
  // 入队
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      entry_1[enq_ptr_1] = in.alloc[i];
      count_1 = count_1 + 1;
      enq_ptr_1 = (enq_ptr_1 + 1) % STQ_NUM;
    }
  }

  // 地址数据写入
  if (in.wr_valid) {
    entry_1[in.wr_idx] = in.write;
  }

  // commit标记为可执行
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit[i]) {
      entry_1[commit_ptr_1].compelete = true;
      commit_ptr_1 = (commit_ptr_1 + 1) % STQ_NUM;
    }
  }

  // 写端口 同时给ld_IQ发送唤醒信息
  if (entry[deq_ptr].valid && entry[deq_ptr].compelete) {
    out.wen = true;
    out.wdata = entry[deq_ptr].data;
    out.waddr = entry[deq_ptr].addr;
    /*out.wstrb = entry[deq_ptr].size;*/
    deq_ptr_1 = (deq_ptr + 1) % STQ_NUM;
  } else {
    out.wen = false;
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
