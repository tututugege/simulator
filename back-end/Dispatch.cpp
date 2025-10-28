#include "config.h"
#include <Dispatch.h>
#include <cstdint>
#include <cvt.h>
#include <util.h>

// 分配rob_idx stq_idx
void Dispatch::comb_alloc() {
  int store_num = 0;

  for (int i = 0; i < 2; i++) {
    io.dis2stq->valid[i] = false;
  }

  uint32_t pre_store_mask = 0;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_alloc[i] = inst_r[i];
    inst_alloc[i].uop.rob_idx = (io.rob2dis->enq_idx << 2) + i;

    if (inst_r[i].uop.type == LOAD) {
      inst_alloc[i].uop.pre_sta_mask = pre_store_mask;
      inst_alloc[i].uop.pre_std_mask = pre_store_mask;
    }

    // 每周期只能dispatch 2个store
    if (inst_r[i].valid && inst_r[i].uop.type == STORE) {
      if (store_num < 2) {
        inst_alloc[i].uop.stq_idx = (io.stq2dis->stq_idx + store_num) % STQ_NUM;
        io.dis2stq->valid[store_num] = true;
        io.dis2stq->tag[store_num] = inst_r[i].uop.tag;
        pre_store_mask = pre_store_mask | (1 << inst_alloc[i].uop.stq_idx);
        store_num++;
      }
    }

    io.dis2rob->valid[i] = inst_r[i].valid;
    io.dis2rob->uop[i] = inst_alloc[i].uop;
  }
}

// busytable bypass
void Dispatch::comb_wake() {
  if (io.prf_awake->wake.valid) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (inst_alloc[i].uop.src1_preg == io.prf_awake->wake.preg) {
        inst_alloc[i].uop.src1_busy = false;
        inst_r_1[i].uop.src1_busy = false;
      }
      if (inst_alloc[i].uop.src2_preg == io.prf_awake->wake.preg) {
        inst_alloc[i].uop.src2_busy = false;
        inst_r_1[i].uop.src2_busy = false;
      }
    }
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (io.iss_awake->wake[i].valid) {
      for (int j = 0; j < FETCH_WIDTH; j++) {
        if (inst_alloc[j].uop.src1_preg == io.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src1_busy = false;
          inst_r_1[j].uop.src1_busy = false;
        }
        if (inst_alloc[j].uop.src2_preg == io.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src2_busy = false;
          inst_r_1[j].uop.src2_busy = false;
        }
      }
    }
  }
}

void Dispatch::comb_dispatch() {
  Inst_entry pre_dis_uop[FETCH_WIDTH * 2];
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
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_ADD;
        pre_dis_uop[2 * i + 1].valid = false;
        break;
      case MUL:
        to_iq[IQ_INTM][i] = true;
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_MUL;
        pre_dis_uop[2 * i + 1].valid = false;
        break;
      case DIV:
        to_iq[IQ_INTD][i] = true;
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_DIV;
        pre_dis_uop[2 * i + 1].valid = false;

        break;
      case BR:
        if (i < FETCH_WIDTH / 2)
          to_iq[IQ_BR0][i] = true;
        else
          to_iq[IQ_BR1][i] = true;

        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_BR;
        pre_dis_uop[2 * i + 1].valid = false;

        break;
      case LOAD:
        to_iq[IQ_LD][i] = true;
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_LOAD;
        pre_dis_uop[2 * i + 1].valid = false;

        break;
      case JALR:
        if (i < FETCH_WIDTH / 2) {
          to_iq[IQ_INTM][i] = true;
          to_iq[IQ_BR0][i] = true;
        } else {
          to_iq[IQ_INTD][i] = true;
          to_iq[IQ_BR1][i] = true;
        }
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_ADD;
        pre_dis_uop[2 * i].uop.imm = 4;
        pre_dis_uop[2 * i].uop.src1_en = false;
        pre_dis_uop[2 * i + 1] = inst_alloc[i];
        pre_dis_uop[2 * i + 1].uop.op = UOP_JUMP;
        pre_dis_uop[2 * i + 1].uop.src1_en = true;
        pre_dis_uop[2 * i + 1].uop.dest_en = false;
        break;
      case JAL:
        if (i < FETCH_WIDTH / 2) {
          to_iq[IQ_INTM][i] = true;
          to_iq[IQ_BR0][i] = true;
        } else {
          to_iq[IQ_INTD][i] = true;
          to_iq[IQ_BR1][i] = true;
        }
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_ADD;
        pre_dis_uop[2 * i].uop.imm = 4;
        pre_dis_uop[2 * i + 1] = inst_alloc[i];
        pre_dis_uop[2 * i + 1].uop.op = UOP_JUMP;
        pre_dis_uop[2 * i + 1].uop.dest_en = false;

        break;
      case STORE:
        to_iq[IQ_STA][i] = true;
        to_iq[IQ_STD][i] = true;
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i].uop.op = UOP_STA;
        pre_dis_uop[2 * i].uop.src1_en = true;
        pre_dis_uop[2 * i].uop.src2_en = false;
        pre_dis_uop[2 * i + 1] = inst_alloc[i];
        pre_dis_uop[2 * i + 1].uop.op = UOP_STD;
        pre_dis_uop[2 * i + 1].uop.src1_en = false;
        pre_dis_uop[2 * i + 1].uop.src2_en = true;

        break;

      // 特殊处理，保证只有一个amo
      case AMO:

        break;
      default:
        to_iq[IQ_INTM][i] = true;
        pre_dis_uop[2 * i] = inst_alloc[i];
        pre_dis_uop[2 * i + 1].valid = false;
        switch (inst_r[i].uop.type) {
        case NONE:
          pre_dis_uop[2 * i].uop.op = UOP_ADD;
          break;
        case CSR:
          pre_dis_uop[2 * i].uop.op = UOP_CSR;
          break;
        case ECALL:
          pre_dis_uop[2 * i].uop.op = UOP_ECALL;
          break;
        case MRET:
          pre_dis_uop[2 * i].uop.op = UOP_MRET;
          break;
        case SRET:
          pre_dis_uop[2 * i].uop.op = UOP_SRET;
          break;
        case SFENCE_VMA:
          pre_dis_uop[2 * i].uop.op = UOP_SFENCE_VMA;
          break;
        case EBREAK:
          pre_dis_uop[2 * i].uop.op = UOP_EBREAK;
          break;
        default:
          exit(1);
          break;
        }
      }
    }
  }

  int ready_num[IQ_NUM];
  for (int i = 0; i < IQ_NUM; i++) {
    if (io.iss2dis->ready[i][1]) {
      ready_num[i] = 2;
    } else if (io.iss2dis->ready[i][0]) {
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
      io.dis2iss->valid[i][j] = port_idx[i][j] != FETCH_WIDTH;
      if (port_idx[i][j] != FETCH_WIDTH) {

        // 根据指令type区分选第一个uop还是第二个uop
        if ((inst_r[port_idx[i][j]].uop.type == JALR ||
             inst_r[port_idx[i][j]].uop.type == JAL) &&
            i >= IQ_BR0) {
          io.dis2iss->uop[i][j] = pre_dis_uop[2 * port_idx[i][j] + 1].uop;
        } else if (inst_r[port_idx[i][j]].uop.type == STORE && i == IQ_STD) {
          io.dis2iss->uop[i][j] = pre_dis_uop[2 * port_idx[i][j] + 1].uop;
        } else {
          io.dis2iss->uop[i][j] = pre_dis_uop[2 * port_idx[i][j]].uop;
        }
      } else {
        // 无关项 可任意
      }
    }
  }
}

void Dispatch::comb_fire() {
  // 判断一个inst是否所有uop都能接收
  bool iss_ready[FETCH_WIDTH];
  for (int i = 0; i < FETCH_WIDTH; i++) {
    iss_ready[i] = true;
    for (int j = 0; j < IQ_NUM; j++) {
      if (to_iq[j][i]) {
        iss_ready[i] = iss_ready[i] && uop_sel[j][i];
      }
    }
  }

  int store_num = 0;
  bool pre_stall = false;
  bool csr_stall = false;
  bool pre_fire = false;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dis2rob->dis_fire[i] =
        (io.dis2rob->valid[i] && io.rob2dis->ready) &&
        (inst_r[i].valid && iss_ready[i]) && !pre_stall && !io.rob2dis->stall &&
        (!is_CSR(inst_r[i].uop.type) || io.rob2dis->empty && !pre_fire);

    if (inst_r[i].uop.type == STORE) {
      io.dis2rob->dis_fire[i] = io.dis2rob->dis_fire[i] &&
                                io.dis2stq->valid[store_num] &&
                                io.stq2dis->ready[store_num] &&
                                !io.dec_bcast->mispred && !io.rob_bcast->flush;

      if (inst_r[i].valid && store_num < 2) {
        io.dis2stq->dis_fire[store_num] = io.dis2rob->dis_fire[i];
        store_num++;
      }
    }

    pre_stall = inst_r[i].valid && !io.dis2rob->dis_fire[i];
    pre_fire = io.dis2rob->dis_fire[i];
  }

  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < 2; j++) {
      if (port_idx[i][j] != FETCH_WIDTH) {
        io.dis2iss->dis_fire[i][j] = io.dis2rob->dis_fire[port_idx[i][j]];
      } else {
        io.dis2iss->dis_fire[i][j] = false;
      }
    }
  }

  io.dis2ren->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dis2ren->ready &= io.dis2rob->dis_fire[i] || !inst_r[i].valid;
  }
}

void Dispatch::comb_pipeline() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.rob_bcast->flush || io.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (io.dis2ren->ready) {
      inst_r_1[i].uop = io.ren2dis->uop[i];
      inst_r_1[i].valid = io.ren2dis->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !io.dis2rob->dis_fire[i];
    }
  }
}

void Dispatch::seq() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }
}
