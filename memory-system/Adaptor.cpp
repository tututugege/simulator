#include "Adaptor.h"
#include "LoadQueue.h"
#include "StoreQueue.h"
#include "AddrTransArb.h"
#include "MemUtil.h"
#include "util.h"
#include "FU.h"

// 发起LSU访存请求
void Adaptor::mem_fire_adpt() {
    if (inst_r[IQ_LS].valid) {
        rs_req->valid_in  = true;
        rs_req->op_in     = is_load(inst_r[IQ_LS].uop.op) ? OP_LD : OP_ST;
        rs_req->mem_sz_in = inst_r[IQ_LS].uop.func3 & 0b11; // 存疑
        rs_req->rs1_in    = inst_r[IQ_LS].uop.src1_rdata;
        rs_req->rs2_in    = inst_r[IQ_LS].uop.imm;
  }
}

void Adaptor::mem_return_adpt() {
    // LSU访存请求被接收，拉高fu->complete
    if (inst_r[IQ_LS].valid && rs_req->ready_out)
        fu->complete = true;
    // load指令访存结果
    if (res_bus->valid_out) {
        exe2prf->entry[IQ_LS].valid = true;
        exe2prf->entry[IQ_LS].uop.dest_en = true;
        exe2prf->entry[IQ_LS].uop.dest_preg = res_bus->dst_preg_out;
        exe2prf->entry[IQ_LS].uop.result = res_bus->data_out[0] & (res_bus->data_out[1] << 8) 
                                    & (res_bus->data_out[2] << 16) & (res_bus->data_out[3] << 24);
    }
}

void Adaptor::stq_commit_adpt() {
    for (int i = 0; i < COMMIT_WIDTH; i++) {
        if (rob_commit->commit_entry[i].valid && is_store(rob_commit->commit_entry[i].uop.op)) {
            *retire_num_in++;
    }
  }
}

void Adaptor::lsq_alloc_forepart() {
    for (int i = 0; i < DECODE_WIDTH; i++) {
        stq_alloc[i].valid_in = ren2stq->valid[i];
        stq_alloc[i].mem_sz_in = ren2stq->mem_sz[i];
        for (int j = 0; j < 4; j++)
            stq_alloc[i].wstrb_in[j] = ren2stq->wstrb[i][j];
        ldq_alloc[i].valid_in = ren2ldq->valid[i];
        ldq_alloc[i].mem_sz_in = ren2ldq->mem_sz[i];
        ldq_alloc[i].dst_preg_in = ren2ldq->dst_reg[i];
        ldq_alloc[i].sign_in = ren2ldq->sign[i];
    }
}

void Adaptor::lsq_alloc_backpart() {
    for (int i = 0; i < DECODE_WIDTH; i++) {
        stq2ren->ready[i] = stq_alloc[i].ready_out;
        ldq2ren->ready[i] = ldq_alloc[i].ready_out;
        stq2ren->stq_idx[i] = stq_alloc[i].stq_idx_out;
        ldq2ren->ldq_idx[i] = ldq_alloc[i].ldq_idx_out;
    }
}