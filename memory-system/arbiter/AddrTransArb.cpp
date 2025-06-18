#include "AddrTransArb.h"
#include "MemUtil.h"

void Addr_Trans_Arb::default_val() {
    out_trans_req->m.mem_sz_out = BYTE;
    out_trans_req->m.vtag_out   = 0;
    out_trans_req->m.index_out  = 0; 
    out_trans_req->m.word_out   = 0;
    out_trans_req->m.offset_out = 0;
    for (int byte = 0; byte < 4; byte++)
        out_trans_req->m.wdata_b4_sft_out[byte] = 0;
}

void Addr_Trans_Arb::arbit_req_forepart() {
    if (stq_trans_req->s.valid_in) {
        out_trans_req->m.valid_out = true;
        out_trans_req->m.op_out    = OP_ST;
        out_trans_req->m.src_out   = STQ;
        out_trans_req->m.vtag_out  = stq_trans_req->s.vtag_in;
        out_trans_req->m.lsq_entry_out = stq_trans_req->s.stq_entry_in;
    }
    else if (ldq_trans_req->s.valid_in) {
        out_trans_req->m.valid_out = true;
        out_trans_req->m.op_out    = OP_LD;
        out_trans_req->m.src_out   = LDQ;
        out_trans_req->m.vtag_out  = ldq_trans_req->s.vtag_in;
        out_trans_req->m.lsq_entry_out = ldq_trans_req->s.ldq_entry_in;
    }
    else {
        out_trans_req->m.valid_out  = rs_trans_req->valid_in;
        out_trans_req->m.op_out     = rs_trans_req->op_in;
        out_trans_req->m.src_out    = RS;
        out_trans_req->m.mem_sz_out = rs_trans_req->mem_sz_in;
        out_trans_req->m.rs1_out    = rs_trans_req->rs1_in;
        out_trans_req->m.rs2_out    = rs_trans_req->rs2_in;
        for (int byte = 0; byte < 4; byte++)
            out_trans_req->m.wdata_b4_sft_out[byte] = rs_trans_req->wdata_b4_sft_in[byte];
        out_trans_req->m.lsq_entry_out = rs_trans_req->lsq_entry_in;
    }
}

void Addr_Trans_Arb::arbit_req_backpart() {
    if (stq_trans_req->s.valid_in) {
        stq_trans_req->s.ready_out = out_trans_req->m.ready_in;
        ldq_trans_req->s.ready_out = false;
        rs_trans_req->ready_out  = false;
    }
    else if (ldq_trans_req->s.valid_in) {
        stq_trans_req->s.ready_out = false;
        ldq_trans_req->s.ready_out = out_trans_req->m.ready_in;
        rs_trans_req->ready_out  = false;
    }
    else {
        stq_trans_req->s.ready_out = false;
        ldq_trans_req->s.ready_out = false;
        rs_trans_req->ready_out  = out_trans_req->m.ready_in;
    }
}