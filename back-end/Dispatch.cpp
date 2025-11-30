#include "config.h"
#include <Dispatch.h>
#include <cvt.h>
#include <util.h>

// 对每个IQ选择最多2个
static wire1_t uop_sel[IQ_NUM][FETCH_WIDTH] = {0};
static wire1_t to_iq[IQ_NUM][FETCH_WIDTH] = {0};
// 实际硬件可以写成4bit独热码用于选择，这里为了方便使用idx
static wire4_t port_idx[IQ_NUM][2];
static Inst_entry inst_alloc[FETCH_WIDTH];

// 分配rob_idx stq_idx
void Dispatch::comb_alloc() {
  int store_num = 0;

  for (int i = 0; i < 2; i++) {
    out.dis2stq->valid[i] = false;
  }

  wire16_t pre_store_mask = 0;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_alloc[i] = inst_r[i];
    inst_alloc[i].uop.rob_idx = (in.rob2dis->enq_idx << 2) + i;
    inst_alloc[i].uop.rob_flag = in.rob2dis->rob_flag;

    if (is_load(inst_r[i].uop)) {
      inst_alloc[i].uop.pre_sta_mask = pre_store_mask;
      inst_alloc[i].uop.pre_std_mask = pre_store_mask;
    }

    // 每周期只能dispatch 2个store
    if (inst_r[i].valid && is_store(inst_r[i].uop)) {
      if (store_num < 2) {
        inst_alloc[i].uop.stq_idx = (in.stq2dis->stq_idx + store_num) % STQ_NUM;
        out.dis2stq->valid[store_num] = true;
        out.dis2stq->tag[store_num] = inst_r[i].uop.tag;
        pre_store_mask = pre_store_mask | (1 << inst_alloc[i].uop.stq_idx);
        store_num++;
      }
    }

    out.dis2rob->valid[i] = inst_r[i].valid;
    out.dis2rob->uop[i] = inst_alloc[i].uop;
  }
}

// busytable bypass
void Dispatch::comb_wake() {
  if (in.prf_awake->wake.valid) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (inst_alloc[i].uop.src1_preg == in.prf_awake->wake.preg) {
        inst_alloc[i].uop.src1_busy = false;
        // inst_alloc[i].uop.src1_latency = 0;
        inst_r_1[i].uop.src1_busy = false;
      }
      if (inst_alloc[i].uop.src2_preg == in.prf_awake->wake.preg) {
        inst_alloc[i].uop.src2_busy = false;
        // inst_alloc[i].uop.src2_latency = 0;
        inst_r_1[i].uop.src2_busy = false;
      }
    }
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (in.iss_awake->wake[i].valid) {
      for (int j = 0; j < FETCH_WIDTH; j++) {
        if (inst_alloc[j].uop.src1_preg == in.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src1_busy = false;
          // inst_alloc[j].uop.src1_latency = in.iss_awake->wake[i].latency;
          // 假如Dispatch卡住，需要修改inrt_r_1
          // 暂时只考虑2周期延迟的乘法指令
          // 如果dispatch卡了一个周期，则无需修改src_latency
          inst_r_1[j].uop.src1_busy = false;
        }
        if (inst_alloc[j].uop.src2_preg == in.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src2_busy = false;
          // inst_alloc[j].uop.src2_latency = in.iss_awake->wake[i].latency;
          inst_r_1[j].uop.src2_busy = false;
        }
      }
    }
  }
}

void Dispatch::comb_dispatch() {
  Inst_entry pre_dis_uop[FETCH_WIDTH * 3];
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      to_iq[i][j] = false;
      uop_sel[i][j] = false;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (inst_r[i].valid) {
      switch (inst_r[i].uop.type) {
      case ADD:
        if (i < FETCH_WIDTH / 2)
          to_iq[IQ_INTM][i] = true;
        else
          to_iq[IQ_INTD][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_ADD;
        break;
      case MUL:
        to_iq[IQ_INTM][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_MUL;
        break;
      case DIV:
        to_iq[IQ_INTD][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_DIV;

        break;
      case BR:
        if (i < FETCH_WIDTH / 2)
          to_iq[IQ_BR0][i] = true;
        else
          to_iq[IQ_BR1][i] = true;

        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_BR;

        break;
      case LOAD:
        to_iq[IQ_LD][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_LOAD;

        break;
      case JALR:
        if (i < FETCH_WIDTH / 2) {
          to_iq[IQ_INTM][i] = true;
          to_iq[IQ_BR0][i] = true;
        } else {
          to_iq[IQ_INTD][i] = true;
          to_iq[IQ_BR1][i] = true;
        }
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_ADD;
        pre_dis_uop[3 * i].uop.imm = 4;
        pre_dis_uop[3 * i].uop.src1_en = false;
        pre_dis_uop[3 * i + 1] = inst_alloc[i];
        pre_dis_uop[3 * i + 1].uop.op = UOP_JUMP;
        pre_dis_uop[3 * i + 1].uop.src1_en = true;
        pre_dis_uop[3 * i + 1].uop.dest_en = false;
        break;
      case JAL:
        if (i < FETCH_WIDTH / 2) {
          to_iq[IQ_INTM][i] = true;
        } else {
          to_iq[IQ_INTD][i] = true;
        }
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_ADD;
        pre_dis_uop[3 * i].uop.imm = 4;

        break;
      case STORE:
        to_iq[IQ_STA][i] = true;
        to_iq[IQ_STD][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        pre_dis_uop[3 * i].uop.op = UOP_STA;
        pre_dis_uop[3 * i].uop.src1_en = true;
        pre_dis_uop[3 * i].uop.src2_en = false;
        pre_dis_uop[3 * i + 1] = inst_alloc[i];
        pre_dis_uop[3 * i + 1].uop.op = UOP_STD;
        pre_dis_uop[3 * i + 1].uop.src1_en = false;
        pre_dis_uop[3 * i + 1].uop.src2_en = true;

        break;

      case AMO:
        if (inst_r[i].uop.amoop == LR) {
          to_iq[IQ_LD][i] = true;
          pre_dis_uop[3 * i] = inst_alloc[i];
          pre_dis_uop[3 * i].uop.op = UOP_LOAD;
          pre_dis_uop[3 * i].uop.src2_en = false;
        } else if (inst_r[i].uop.amoop == SC) {
          if (i < FETCH_WIDTH / 2)
            to_iq[IQ_INTM][i] = true;
          else
            to_iq[IQ_INTD][i] = true;
          to_iq[IQ_STA][i] = true;
          to_iq[IQ_STD][i] = true;

          pre_dis_uop[3 * i] = inst_alloc[i];
          pre_dis_uop[3 * i].uop.op = UOP_ADD;
          pre_dis_uop[3 * i].uop.src1_preg = 0;
          pre_dis_uop[3 * i].uop.src1_busy = false;
          pre_dis_uop[3 * i].uop.src2_is_imm = true;
          pre_dis_uop[3 * i].uop.src2_en = false;
          pre_dis_uop[3 * i].uop.imm = 0;
          pre_dis_uop[3 * i + 1] = inst_alloc[i];
          pre_dis_uop[3 * i + 1].uop.op = UOP_STA;
          pre_dis_uop[3 * i + 1].uop.src2_en = false;
          pre_dis_uop[3 * i + 2] = inst_alloc[i];
          pre_dis_uop[3 * i + 2].uop.op = UOP_STD;
          pre_dis_uop[3 * i + 2].uop.src1_en = false;
        } else {
          to_iq[IQ_LD][i] = true;
          to_iq[IQ_STA][i] = true;
          to_iq[IQ_STD][i] = true;
          pre_dis_uop[3 * i] = inst_alloc[i];
          pre_dis_uop[3 * i].uop.op = UOP_LOAD;
          pre_dis_uop[3 * i].uop.src2_en = false;
          pre_dis_uop[3 * i].uop.imm = 0;
          pre_dis_uop[3 * i + 1] = inst_alloc[i];
          pre_dis_uop[3 * i + 1].uop.op = UOP_STA;
          pre_dis_uop[3 * i + 1].uop.src2_en = false;
          pre_dis_uop[3 * i + 1].uop.imm = 0;
          pre_dis_uop[3 * i + 2] = inst_alloc[i];
          pre_dis_uop[3 * i + 2].uop.op = UOP_STD;
          pre_dis_uop[3 * i + 2].uop.src1_preg = inst_r[i].uop.dest_preg;
          pre_dis_uop[3 * i + 2].uop.src1_busy = true;
        }
        break;
      default:
        to_iq[IQ_INTM][i] = true;
        pre_dis_uop[3 * i] = inst_alloc[i];
        switch (inst_r[i].uop.type) {
        case NONE:
          pre_dis_uop[3 * i].uop.op = UOP_ADD;
          break;
        case CSR:
          pre_dis_uop[3 * i].uop.op = UOP_CSR;
          break;
        case ECALL:
          pre_dis_uop[3 * i].uop.op = UOP_ECALL;
          break;
        case MRET:
          pre_dis_uop[3 * i].uop.op = UOP_MRET;
          break;
        case SRET:
          pre_dis_uop[3 * i].uop.op = UOP_SRET;
          break;
        case SFENCE_VMA:
          pre_dis_uop[3 * i].uop.op = UOP_SFENCE_VMA;
          break;
        case EBREAK:
          pre_dis_uop[3 * i].uop.op = UOP_EBREAK;
          break;
        default:
          exit(1);
          break;
        }
      }
    }
  }

  wire2_t ready_num[IQ_NUM];
  for (int i = 0; i < IQ_NUM; i++) {
    if (in.iss2dis->ready[i][1]) {
      ready_num[i] = 2;
    } else if (in.iss2dis->ready[i][0]) {
      ready_num[i] = 1;
    } else {
      ready_num[i] = 0;
    }
  }

  for (int i = 0; i < IQ_NUM; i++) {
    int num = 0;
    for (int j = 0; j < FETCH_WIDTH && num < ready_num[i]; j++) {
      if (to_iq[i][j]) {
        uop_sel[i][j] = true;
        num++;
      }
    }
  }

  // 根据uop_sel 每个iq的分派指令
  // 每个IQ的uop_sel有1100 1010 1001 0110 0101 0011 1000 0100 0010 0001 0000
  // 几种情况 根据这几种情况生成对应的对应port_idx
  for (int i = 0; i < IQ_NUM; i++) {
    int port = 0;
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (uop_sel[i][j]) {
        port_idx[i][port] = j;
        port++;
      }
    }

    for (; port < 2; port++) {
      port_idx[i][port] = FETCH_WIDTH; // 表示该dispatch port该周期未被使用
    }
  }

  // 根据port_idx选择指令
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < 2; j++) {
      out.dis2iss->valid[i][j] = port_idx[i][j] != FETCH_WIDTH;
      if (port_idx[i][j] != FETCH_WIDTH) {

        // 根据指令type区分选第一个uop还是第二个uop
        if (inst_r[port_idx[i][j]].uop.type == JALR && i >= IQ_BR0) {
          out.dis2iss->uop[i][j] = pre_dis_uop[3 * port_idx[i][j] + 1].uop;
        } else if (inst_r[port_idx[i][j]].uop.type == STORE && i == IQ_STD) {
          out.dis2iss->uop[i][j] = pre_dis_uop[3 * port_idx[i][j] + 1].uop;
        } else if (inst_r[port_idx[i][j]].uop.type == AMO && i == IQ_STA) {
          out.dis2iss->uop[i][j] = pre_dis_uop[3 * port_idx[i][j] + 1].uop;
        } else if (inst_r[port_idx[i][j]].uop.type == AMO && i == IQ_STD) {
          out.dis2iss->uop[i][j] = pre_dis_uop[3 * port_idx[i][j] + 2].uop;
        } else {
          out.dis2iss->uop[i][j] = pre_dis_uop[3 * port_idx[i][j]].uop;
        }
      } else {
        // 无关项 可任意
      }
    }
  }
}

void Dispatch::comb_fire() {
  // 判断一个inst是否所有uop都能接收
  wire1_t iss_ready[FETCH_WIDTH];
  for (int i = 0; i < FETCH_WIDTH; i++) {
    iss_ready[i] = true;
    for (int j = 0; j < IQ_NUM; j++) {
      if (to_iq[j][i]) {
        iss_ready[i] = iss_ready[i] && uop_sel[j][i];
      }
    }
  }

  int store_num = 0;
  wire1_t pre_stall = false;
  wire1_t csr_stall = false;
  wire1_t pre_fire = false;
  wire1_t pre_is_flush = false;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.dis2rob->dis_fire[i] =
        (out.dis2rob->valid[i] && in.rob2dis->ready) &&
        (inst_r[i].valid && iss_ready[i]) && !pre_stall && !in.rob2dis->stall &&
        (!is_CSR(inst_r[i].uop.type) || in.rob2dis->empty && !pre_fire) &&
        !pre_is_flush && !in.dec_bcast->mispred && !in.rob_bcast->flush;

    if (is_store(inst_r[i].uop)) {
      out.dis2rob->dis_fire[i] = out.dis2rob->dis_fire[i] &&
                                 out.dis2stq->valid[store_num] &&
                                 in.stq2dis->ready[store_num];

      if (inst_r[i].valid && store_num < 2) {
        out.dis2stq->dis_fire[store_num] = out.dis2rob->dis_fire[i];
        store_num++;
      }
    }

    pre_stall = inst_r[i].valid && !out.dis2rob->dis_fire[i];
    pre_fire = out.dis2rob->dis_fire[i];
    pre_is_flush = inst_r[i].valid && is_flush_inst(inst_r[i].uop);

#ifdef CONFIG_PERF_COUNTER
    if (inst_r[i].valid && !out.dis2rob->dis_fire[i]) {
      if (!in.rob2dis->ready) {
        perf.rob_entry_stall++;
      } else if (is_store(inst_r[i].uop)) {

      } else if (!iss_ready[i]) {
        perf.isu_entry_stall++;
      }
    }
#endif
  }

  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < 2; j++) {
      if (port_idx[i][j] != FETCH_WIDTH) {
        out.dis2iss->dis_fire[i][j] = out.dis2rob->dis_fire[port_idx[i][j]];
      } else {
        out.dis2iss->dis_fire[i][j] = false;
      }
    }
  }

  out.dis2ren->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.dis2ren->ready &= out.dis2rob->dis_fire[i] || !inst_r[i].valid;
  }
}

void Dispatch::comb_pipeline() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (in.rob_bcast->flush || in.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (out.dis2ren->ready) {
      inst_r_1[i].uop = in.ren2dis->uop[i];
      inst_r_1[i].valid = in.ren2dis->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !out.dis2rob->dis_fire[i];
    }
  }
}

void Dispatch::seq() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }
}
