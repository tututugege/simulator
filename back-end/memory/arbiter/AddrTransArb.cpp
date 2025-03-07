#include "AddrTransArb.h"

void Addr_Trans_Arb::arbit_req_forepart() {
    if (stq_trans_req->s.valid_in) {
        out_trans_req->m.valid_out = true;
        out_trans_req->m.op_out    = op_t::OP_ST;
        out_trans_req->m.src_out   = src_t::STQ;
        out_trans_req->m.vtag_out  = stq_trans_req->s.vtag_in;
    }
    else if (ldq_trans_req->s.valid_in) {
        out_trans_req->m.valid_out = true;
        out_trans_req->m.op_out    = op_t::OP_LD;
        out_trans_req->m.src_out   = src_t::LDQ;
        out_trans_req->m.vtag_out  = ldq_trans_req->s.vtag_in;
    }
    else {
        out_trans_req->m.valid_out  = lsu_req->s.valid_in;
        out_trans_req->m.op_out     = lsu_req->s.op_in;
        out_trans_req->m.src_out    = src_t::RS;
        out_trans_req->m.vtag_out   = lsu_req->s.vtag_in;
        out_trans_req->m.index_out  = lsu_req->s.index_in;
        out_trans_req->m.word_out   = lsu_req->s.word_in;
        out_trans_req->m.offset_out = lsu_req->s.offset_in;
        for (int byte = 0; byte < 4; byte++)
            out_trans_req->m.wdata_b4_sft_out[byte] = lsu_req->s.wdata_b4_sft_in[byte];
        out_trans_req->m.lsq_entry_out = lsu_req->s.lsq_entry_in;
    }
}

void Addr_Trans_Arb::arbit_req_backpart() {
    if (stq_trans_req->s.valid_in) {
        stq_trans_req->s.ready_out = out_trans_req->m.ready_in;
        ldq_trans_req->s.ready_out = false;
        lsu_req->s.ready_out = false;
    }
    else if (ldq_trans_req->s.valid_in) {
        stq_trans_req->s.ready_out = false;
        ldq_trans_req->s.ready_out = out_trans_req->m.ready_in;
        lsu_req->s.ready_out = false;
    }
    else {
        stq_trans_req->s.ready_out = false;
        ldq_trans_req->s.ready_out = false;
        lsu_req->s.ready_out = out_trans_req->m.ready_in;
    }
}