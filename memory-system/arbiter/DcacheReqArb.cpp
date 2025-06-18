#include "DcacheReqArb.h"
#include "Dcache.h"

void Dcache_Req_Arb::arbit_req_forepart() {
    if (ldq_cache_req->s.valid_in) {
        cache_req->m.valid_out    = ldq_cache_req->s.valid_in;
        cache_req->m.op_out       = ldq_cache_req->s.op_in;
        cache_req->m.tagv_out     = ldq_cache_req->s.tagv_in;
        cache_req->m.tag_out      = ldq_cache_req->s.tag_in;
        cache_req->m.index_out    = ldq_cache_req->s.index_in;
        cache_req->m.word_out     = ldq_cache_req->s.word_in;
        cache_req->m.offset_out   = ldq_cache_req->s.offset_in;
        cache_req->m.lsq_entry_out = ldq_cache_req->s.lsq_entry_in;
    }
    else if (lsu_cache_req->s.valid_in) {
        cache_req->m.valid_out    = lsu_cache_req->s.valid_in;
        cache_req->m.op_out       = lsu_cache_req->s.op_in;
        cache_req->m.tagv_out     = lsu_cache_req->s.tagv_in;
        cache_req->m.tag_out      = lsu_cache_req->s.tag_in;
        cache_req->m.index_out    = lsu_cache_req->s.index_in;
        cache_req->m.word_out     = lsu_cache_req->s.word_in;
        cache_req->m.offset_out   = lsu_cache_req->s.offset_in;
        for (int byte = 0 ; byte < 4; byte++) {
            cache_req->m.wdata_aft_sft_out[byte] = lsu_cache_req->s.wdata_aft_sft_in[byte];
            cache_req->m.wstrb_out[byte] = lsu_cache_req->s.wstrb_in[byte];
        }
        cache_req->m.lsq_entry_out = lsu_cache_req->s.lsq_entry_in;
        stq_cache_req->s.addr_ok_out = false;
    }
    else {
        cache_req->m.valid_out    = stq_cache_req->s.valid_in;
        cache_req->m.op_out       = stq_cache_req->s.op_in;
        cache_req->m.tagv_out     = stq_cache_req->s.tagv_in;
        cache_req->m.tag_out      = stq_cache_req->s.tag_in;
        cache_req->m.index_out    = stq_cache_req->s.index_in;
        cache_req->m.word_out     = stq_cache_req->s.word_in;
        cache_req->m.offset_out   = stq_cache_req->s.offset_in;
        for (int byte = 0 ; byte < 4; byte++) {
            cache_req->m.wdata_aft_sft_out[byte] = stq_cache_req->s.wdata_aft_sft_in[byte];
            cache_req->m.wstrb_out[byte] = stq_cache_req->s.wstrb_in[byte];
        }
        cache_req->m.lsq_entry_out = stq_cache_req->s.lsq_entry_in;
    }
}

void Dcache_Req_Arb::arbit_req_backpart() {
    if (ldq_cache_req->s.valid_in) {
        ldq_cache_req->s.addr_ok_out = cache_req->m.addr_ok_in;
        lsu_cache_req->s.addr_ok_out = false;
        stq_cache_req->s.addr_ok_out = false;
    }
    else if (lsu_cache_req->s.valid_in) {
        ldq_cache_req->s.addr_ok_out = false;
        lsu_cache_req->s.addr_ok_out = cache_req->m.addr_ok_in;
        stq_cache_req->s.addr_ok_out = false;
    }
    else {
        ldq_cache_req->s.addr_ok_out = false;
        lsu_cache_req->s.addr_ok_out = false;
        stq_cache_req->s.addr_ok_out = cache_req->m.addr_ok_in;
    }
}