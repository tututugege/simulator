#include "LSU.h"
#include "AddrTransArb.h"
#include "LoadQueue.h"
#include "StoreQueue.h"
#include "Dcache.h"
#include "util.h"

void LSU::default_val() {
    stage2_info_r_io.valid = false;

    lsu_cache_req->m.valid_out        = false;
    lsu_cache_req->m.op_out           = op_t::OP_LD;
    lsu_cache_req->m.tagv_out         = false;
    lsu_cache_req->m.index_out        = 0;
    lsu_cache_req->m.word_out         = 0;
    lsu_cache_req->m.offset_out       = 0;
    lsu_cache_req->m.lsq_entry_out    = 0;

    mmu_req->m.valid_out = false;
    mmu_req->m.vtag_out  = 0;
    mmu_req->m.op_out    = op_t::OP_LD;
    out_trans_req->s.ready_out = false;

    stage2_info_r_io.op        = op_t::OP_LD;
    stage2_info_r_io.src       = src_t::LDQ;
    stage2_info_r_io.mem_sz    = mem_sz_t::BYTE;
    stage2_info_r_io.vtag      = 0;
    stage2_info_r_io.index     = 0;
    stage2_info_r_io.word      = 0;
    stage2_info_r_io.offset    = 0;
    stage2_info_r_io.lsq_entry = 0;
    for (int byte = 0; byte < 4; byte++) 
        stage2_info_r_io.wdata_b4_sft[byte] = 0;

    ldq_fill_req->m.valid_out = false;
    ldq_fill_req->m.ldq_entry_out = 0;
    ldq_fill_req->m.src_out       = src_t::LDQ;
    ldq_fill_req->m.tag_out = 0;
    ldq_fill_req->m.addr_trans_out = false;
    ldq_fill_req->m.paddrv_out = false;
    ldq_fill_req->m.index_out     = 0;
    ldq_fill_req->m.word_out      = 0;
    ldq_fill_req->m.offset_out    = 0;

    stq_fill_req->m.valid_out     = false;
    stq_fill_req->m.stq_entry_out = 0;
    stq_fill_req->m.src_out       = src_t::LDQ;
    stq_fill_req->m.tag_out = 0;
    stq_fill_req->m.addr_trans_out = false;
    stq_fill_req->m.paddrv_out = false;
    stq_fill_req->m.index_out     = 0;
    stq_fill_req->m.word_out      = 0;
    stq_fill_req->m.offset_out    = 0;

    for (int byte = 0; byte < 4; byte++) 
        stq_fill_req->m.wdata_b4_sft_out[byte] = 0;
}

void LSU::stage1_forepart() {
    if (out_trans_req->s.valid_in) {
        if (out_trans_req->s.op_in == op_t::OP_LD) {
            lsu_cache_req->m.valid_out        = true;
            lsu_cache_req->m.op_out           = op_t::OP_LD;
            lsu_cache_req->m.tagv_out         = false;
            lsu_cache_req->m.index_out        = out_trans_req->s.index_in;
            lsu_cache_req->m.word_out         = out_trans_req->s.word_in;
            lsu_cache_req->m.offset_out       = out_trans_req->s.offset_in;
            lsu_cache_req->m.lsq_entry_out    = out_trans_req->s.lsq_entry_in;
        }
        else {
            lsu_cache_req->m.valid_out = false;
            mmu_req->m.valid_out   = true;
            mmu_req->m.vtag_out    = out_trans_req->s.vtag_in;
            mmu_req->m.op_out      = op_t::OP_ST;
            stage2_info_r_io.valid      = true;
            out_trans_req->s.ready_out  = true;
        }
    }
}

void LSU::stage1_backpart() {
    if (out_trans_req->s.valid_in) {
        // 
        if (out_trans_req->s.op_in == op_t::OP_LD && lsu_cache_req->m.valid_out && lsu_cache_req->m.addr_ok_in) {
            mmu_req->m.valid_out = true;
            mmu_req->m.vtag_out  = out_trans_req->s.vtag_in;
            mmu_req->m.op_out    = op_t::OP_LD;
            stage2_info_r_io.valid      = true;
            out_trans_req->s.ready_out  = true;
        }

        stage2_info_r_io.op        = out_trans_req->s.op_in;
        stage2_info_r_io.src       = out_trans_req->s.src_in;
        stage2_info_r_io.mem_sz    = out_trans_req->s.mem_sz_in;
        stage2_info_r_io.vtag      = out_trans_req->s.vtag_in;
        stage2_info_r_io.index     = out_trans_req->s.index_in;
        stage2_info_r_io.word      = out_trans_req->s.word_in;
        stage2_info_r_io.offset    = out_trans_req->s.offset_in;
        stage2_info_r_io.lsq_entry = out_trans_req->s.lsq_entry_in;
        for (int byte = 0; byte < 4; byte++) 
            stage2_info_r_io.wdata_b4_sft[byte] = out_trans_req->s.wdata_b4_sft_in[byte];
    }
}

void LSU::stage2() {
    if (stage2_info_r.valid) {
        if (stage2_info_r.src != src_t::RS && stage2_info_r.op == op_t::OP_LD) {
            ldq_fill_req->m.valid_out     = true;
            ldq_fill_req->m.ldq_entry_out = stage2_info_r.lsq_entry;
            ldq_fill_req->m.src_out       = src_t::LDQ;
            if (mmu_resp->s.okay_in) {
                ldq_fill_req->m.tag_out = mmu_resp->s.ptag_in;
                ldq_fill_req->m.addr_trans_out = true;
                ldq_fill_req->m.paddrv_out = true;
            }
            else if (mmu_resp->s.miss_in) {
                ldq_fill_req->m.tag_out = stage2_info_r.vtag;
                ldq_fill_req->m.addr_trans_out = false;
                ldq_fill_req->m.paddrv_out = false;
            }
            else {
            }
        }
        else if (stage2_info_r.src != src_t::RS && stage2_info_r.op == op_t::OP_ST) {
            stq_fill_req->m.valid_out     = true;
            stq_fill_req->m.stq_entry_out = stage2_info_r.lsq_entry;
            stq_fill_req->m.src_out       = src_t::STQ;
            if (mmu_resp->s.okay_in) {
                stq_fill_req->m.tag_out = mmu_resp->s.ptag_in;
                stq_fill_req->m.addr_trans_out = true;
                stq_fill_req->m.paddrv_out = true;
            }
            else if (mmu_resp->s.miss_in) {
                stq_fill_req->m.tag_out = stage2_info_r.vtag;
                stq_fill_req->m.addr_trans_out = false;
                stq_fill_req->m.paddrv_out = false;
            }
            else {
            }
        }
        else if (stage2_info_r.op == op_t::OP_LD) {
            ldq_fill_req->m.valid_out     = true;
            ldq_fill_req->m.ldq_entry_out = stage2_info_r.lsq_entry;
            ldq_fill_req->m.src_out       = src_t::RS;
            ldq_fill_req->m.index_out     = stage2_info_r.index;
            ldq_fill_req->m.word_out      = stage2_info_r.word;
            ldq_fill_req->m.offset_out    = stage2_info_r.offset;
            if (mmu_resp->s.okay_in) {
                ldq_fill_req->m.tag_out = mmu_resp->s.ptag_in;
                ldq_fill_req->m.addr_trans_out = true;
                ldq_fill_req->m.paddrv_out = true;
            }
            else if (mmu_resp->s.miss_in) {
                ldq_fill_req->m.tag_out = stage2_info_r.vtag;
                ldq_fill_req->m.addr_trans_out = false;
                ldq_fill_req->m.paddrv_out = false;
            }
            else {
            }
        }
        else {
            stq_fill_req->m.valid_out     = true;
            stq_fill_req->m.stq_entry_out = stage2_info_r.lsq_entry;
            stq_fill_req->m.src_out       = src_t::RS;
            stq_fill_req->m.index_out     = stage2_info_r.index;
            stq_fill_req->m.word_out      = stage2_info_r.word;
            stq_fill_req->m.offset_out    = stage2_info_r.offset;
            for (int byte = 0; byte < 4; byte++) 
                stq_fill_req->m.wdata_b4_sft_out[byte] = stage2_info_r.wdata_b4_sft[byte];
            if (mmu_resp->s.okay_in) {
                stq_fill_req->m.tag_out = mmu_resp->s.ptag_in;
                stq_fill_req->m.addr_trans_out = true;
                stq_fill_req->m.paddrv_out = true;
            }
            else if (mmu_resp->s.miss_in) {
                stq_fill_req->m.tag_out = stage2_info_r.vtag;
                stq_fill_req->m.addr_trans_out = false;
                stq_fill_req->m.paddrv_out = false;
            }
            else {
            }
        }
    }
}

void LSU::seq() {
    stage2_info_r = stage2_info_r_io;
}