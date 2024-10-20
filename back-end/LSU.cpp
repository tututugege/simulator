#include <LSU.h>
#include <config.h>
#include <util.h>
void LDQ::wake_up_pre_store() {
  for (int i = deq_ptr; i < enq_ptr; i = LOOP_INC(i, LDQ_NUM)) {
    if (in.st_valid)
      entry_1.pre_store[in.stq_idx] = false;
  }
}

void LDQ::comb_commit() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit[i]) {
      entry_1[deq_ptr_1].valid = false;
      deq_ptr_1 = LOOP_INC(deq_ptr_1, LDQ_NUM);
    }
  }
}

void LDQ::comb_mem_fire(STQ *stq) {
  int fire_idx;
  uint32_t data_from_mem;
  uint32_t data_from_stq;
  bool forward_valid;
  out.mem_fire_valid = false;

  // 从ldq中选出最老的已经issue但尚未fire的指令
  for (fire_idx = deq_ptr; fire_idx != enq_ptr; fire_idx = LOOP_INC(fire_idx, LDQ_NUM)) {
    if (entry[fire_idx].issue && !entry[fire_idx].fire) {
      entry_1[fire_idx].issue = true;
      entry_1[fire_idx].comb_complete = true;
      out.mem_fire_valid = true;
      break;
    }
  }
  
  // 选出指令有效，发起访存请求并前递
  if (out.mem_fire_valid) {

    cvt_number_to_bit(output_data + POS_OUT_LOAD_ADDR, entry[fire_idx].addr, 32);
    load_data();
    data_from_mem = cvt_bit_to_number_unsigned(input_data + POS_IN_LOAD_DATA, 32);

    stq->in.forward_addr = entry[fire_idx].addr;
    stq->in.forward_size = entry[fire_idx].size;
    stq->in.ld_pre_store = entry[fire_idx].pre_store;
    stq->store_2_load_forward(&data_from_stq);

    out.dest_preg = entry[fire_idx].dest_preg;
    out.data = stq->out.forward_valid ? data_from_stq : data_from_mem;
  }
}

void LDQ::wake_up_addr() {
  if (in.from_ld_iq_valid) {
    entry_1[in.ldq_idx].addr = in.addr;
    entry_1[in.ldq_idx].issue = true;
  }
}

void LDQ::comb_alloc() {
  int num = count;
  int idx = enq_ptr;
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i]) {
      out.to_dis_ready[i] = true;
    } else {
      if (num < STQ_NUM) {
        out.to_dis_ready[i] = true;
        out.enq_idx[i] = idx;
        num++;
        idx = (idx + 1) % STQ_NUM;
      } else {
        out.to_dis_ready[i] = false;
      }
    }
  }
}

void STQ::store_2_load_forward(int *data_from_stq) {
  out.forward_valid = false;
  for (int i = deq_ptr; i != enq_ptr; i = LOOP_INC(i, STQ_NUM)) {
    if (in.ld_pre_store[i] && addr_overlap(in.forward_addr, in.forward_size, 
                                           entry[i].addr, entry[i].size)) {
      *data_from_stq = ;
      out.forward_valid = true;
    }
  }
}

void STQ::comb_commit() {
  // commit标记为可执行
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit[i]) {
      entry_1[commit_ptr_1].compelete = true;
      commit_ptr_1 = (commit_ptr_1 + 1) % STQ_NUM;
    }
  }
}

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

void STQ::to_dis_pre_store() {
  // 指令store依赖信息
  for (int i = 0; i < STQ_NUM; i++) {
    out.to_dis_pre_store[i] = entry_1[i].valid && entry_1[i].issue;
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
