#include "Mshr.h"
#include "Dcache.h"
#include "MMU.h"
#include "MemUtil.h"

void Dcache::default_val() {
    cache_res->m.valid_out = false;
    cache_res->m.op_out        = OP_LD;
    cache_res->m.hit_out       = false;
    cache_res->m.miss_out      = false;
    cache_res->m.abort_out     = false;
    cache_res->m.lsq_entry_out = 0;
    for (int byte = 0; byte < 4; byte++)
        cache_res->m.data_out[byte] = 0;

    mshr_alc_req->m.valid_out = false;
    mshr_alc_req->m.tag_out   = 0;
    mshr_alc_req->m.index_out = 0;
    mshr_alc_req->m.word_out  = 0;
    for (int byte = 0; byte < 4; byte++) {
        mshr_alc_req->m.wdata_aft_sft_out[byte] = 0;
        mshr_alc_req->m.wdata_strb_out[byte]    = false;
    }

    req_info_r2_io.valid = false;
    req_info_r2_io.abort = false;
    req_info_r2_io.op        = OP_LD;
    req_info_r2_io.tagv      = false;
    req_info_r2_io.tag       = 0;
    req_info_r2_io.index     = 0;
    req_info_r2_io.word      = 0;
    req_info_r2_io.offset    = 0;
    req_info_r2_io.lsq_entry = 0;
    for (int byte = 0; byte < 4; byte++) {
        req_info_r2_io.wdata_aft_sft[byte] = 0;
        req_info_r2_io.wstrb[byte]         = false;
    }

    req_info_r3_io.valid = false;
    req_info_r3_io.abort = false;
    req_info_r3_io.op        = OP_LD;
    req_info_r3_io.tagv      = false;
    req_info_r3_io.tag       = 0;
    req_info_r3_io.index     = 0;
    req_info_r3_io.word      = 0;
    req_info_r3_io.offset    = 0;
    req_info_r3_io.lsq_entry = 0;
    for (int byte = 0; byte < 4; byte++) {
        req_info_r3_io.wdata_aft_sft[byte] = 0;
        req_info_r3_io.wstrb[byte]         = false;
    }
}

void Dcache::stage1() {
    cache_req->s.addr_ok_out = !mshr_info->s.cache_refill_in && !(mshr_info->s.cache_replace_in && cache_req->s.op_in == OP_ST);
    if (cache_req->s.valid_in && cache_req->s.addr_ok_out)
        for (int way = 0; way < 2; way++) {
            tagv_ram_io[way].en = true;
            tagv_ram_io[way].addr = cache_req->s.index_in;
            if (cache_req->s.op_in == OP_LD) {
                data_ram_io[way][cache_req->s.word_in].en = true;
                data_ram_io[way][cache_req->s.word_in].addr = cache_req->s.index_in;
            }
        }
    req_info_r2_io.valid     = cache_req->s.valid_in && cache_req->s.addr_ok_out;
    if (cache_res->m.abort_out && cache_res->m.op_out == OP_ST && cache_req->s.op_in == OP_ST)
        req_info_r2_io.abort = true;
    else
        req_info_r2_io.abort = false;
    req_info_r2_io.op        = cache_req->s.op_in;
    req_info_r2_io.tagv      = cache_req->s.tagv_in;
    req_info_r2_io.tag       = cache_req->s.tag_in;
    req_info_r2_io.index     = cache_req->s.index_in;
    req_info_r2_io.word      = cache_req->s.word_in;
    req_info_r2_io.offset    = cache_req->s.offset_in;
    req_info_r2_io.lsq_entry = cache_req->s.lsq_entry_in;
    for (int byte = 0; byte < 4; byte++) {
        req_info_r2_io.wdata_aft_sft[byte] = cache_req->s.wdata_aft_sft_in[byte];
        req_info_r2_io.wstrb[byte]         = cache_req->s.wstrb_in[byte];
    }
}

void Dcache::stage2() {
    uint32_t tag;
    if (req_info_r2.valid && !req_info_r2.tagv && mmu_resp->s.okay_in) {
        tag = mmu_resp->s.ptag_in;
        req_info_r3_io.tagv = true;
    }
    else {
        tag = req_info_r2.tag;
        req_info_r3_io.tagv = req_info_r2.tagv;
    }

    for (int way = 0; way < 2; way++)
        if (tagv_ram[way][tagv_ram_io[way].addr].v && tagv_ram[way][tagv_ram_io[way].addr].tag == tag) {
            hit_info_r3_io.hit = true;
            hit_info_r3_io.hit_way = way;
            for (int byte = 0; byte < 4; byte++)
                hit_info_r3_io.rdata[byte] = data_ram[way][req_info_r2.word][data_ram_io[way][req_info_r2.word].addr].data[byte];
        }

    req_info_r3_io.valid     = req_info_r2.valid;
    req_info_r3_io.op        = req_info_r2.op;
    req_info_r3_io.tag       = tag;
    req_info_r3_io.index     = req_info_r2.index;
    req_info_r3_io.word      = req_info_r2.word;
    req_info_r3_io.offset    = req_info_r2.offset;
    req_info_r3_io.lsq_entry = req_info_r2.lsq_entry;
    for (int byte = 0; byte < 4; byte++) {
        req_info_r3_io.wdata_aft_sft[byte] = req_info_r2.wdata_aft_sft[byte];
        req_info_r3_io.wstrb[byte]         = req_info_r2.wstrb[byte];
    }

    if (req_info_r2.valid && !req_info_r2.tagv && !mmu_resp->s.okay_in)
        req_info_r3_io.abort = true;
    else if (req_info_r2.valid && mshr_info->s.cache_replace_in) {
        if (req_info_r2.op == OP_ST)
            req_info_r3_io.abort = true;
        else if (req_info_r2.op == OP_LD && !hit_info_r3_io.hit)

            req_info_r3_io.abort = true;
    }
    else if (req_info_r2.valid && mshr_info->s.cache_refill_in) {
        if (!hit_info_r3_io.hit)
            req_info_r3_io.abort = true;
    }
    else if (req_info_r2.valid && cache_res->m.abort_out && cache_res->m.op_out == OP_ST && req_info_r2.op == OP_ST)
        req_info_r3_io.abort = true; 
}

void Dcache::stage3_forepart() {
    abort_w3 = req_info_r3.abort || (mshr_info->s.cache_replace_in || cache_req->s.valid_in && cache_req->s.op_in == OP_LD) && req_info_r3.valid && req_info_r3.op == OP_ST && hit_info_r3.hit;

    if (req_info_r3.valid && req_info_r3.op == OP_LD && hit_info_r3.hit && !abort_w3) {
        cache_res->m.valid_out     = true;
        cache_res->m.op_out        = OP_LD;
        cache_res->m.hit_out       = true;
        cache_res->m.miss_out      = false;
        cache_res->m.abort_out     = false;
        cache_res->m.lsq_entry_out = req_info_r3.lsq_entry;
        for (int byte = 0; byte < 4; byte++)
            cache_res->m.data_out[byte] = hit_info_r3.rdata[byte];
    }
    else if (req_info_r3.valid && req_info_r3.op == OP_LD && !hit_info_r3.hit && !abort_w3) {
        mshr_alc_req->m.valid_out = true;
        mshr_alc_req->m.tag_out   = req_info_r3.tag;
        mshr_alc_req->m.index_out = req_info_r3.index;
        mshr_alc_req->m.word_out  = req_info_r3.word;
    }
    else if (req_info_r3.valid && req_info_r3.op == OP_ST && hit_info_r3.hit && !abort_w3) {
        data_ram_io[hit_info_r3.hit_way][req_info_r3.word].en = true;
        for (int byte = 0; byte < 4; byte++) {
            data_ram_io[hit_info_r3.hit_way][req_info_r3.word].we[byte] = req_info_r3.wstrb[byte];
            data_ram_io[hit_info_r3.hit_way][req_info_r3.word].din[byte] = req_info_r3.wdata_aft_sft[byte];
        }
        data_ram_io[hit_info_r3.hit_way][req_info_r3.word].addr = req_info_r3.index;
        
        cache_res->m.valid_out     = true;
        cache_res->m.op_out        = OP_ST;
        cache_res->m.hit_out       = true;
        cache_res->m.miss_out      = false;
        cache_res->m.abort_out     = false;
        cache_res->m.lsq_entry_out = req_info_r3.lsq_entry;
    }
    else if (req_info_r3.valid && req_info_r3.op == OP_ST && !hit_info_r3.hit && !abort_w3) {
        mshr_alc_req->m.valid_out = true;
        mshr_alc_req->m.tag_out   = req_info_r3.tag;
        mshr_alc_req->m.index_out = req_info_r3.index;
        mshr_alc_req->m.word_out  = req_info_r3.word;
        for (int byte = 0; byte < 4; byte++) {
            mshr_alc_req->m.wdata_aft_sft_out[byte] = req_info_r3.wdata_aft_sft[byte];
            mshr_alc_req->m.wdata_strb_out[byte]    = req_info_r3.wstrb[byte];
        }
    }
    else if (req_info_r3.valid && abort_w3) {
        cache_res->m.valid_out      = true;
        cache_res->m.op_out         = req_info_r3.op;
        cache_res->m.hit_out        = false;
        cache_res->m.miss_out       = false;
        cache_res->m.abort_out      = true;
        cache_res->m.lsq_entry_out  = req_info_r3.lsq_entry;
    }
}

void Dcache::stage3_backpart() {
    if (req_info_r3.valid && req_info_r3.op == OP_LD && !hit_info_r3.hit && !abort_w3) {
        if (!mshr_alc_req->m.ok_in) {
            cache_res->m.valid_out      = true;
            cache_res->m.op_out         = OP_LD;
            cache_res->m.hit_out        = false;
            cache_res->m.miss_out       = false;
            cache_res->m.abort_out      = true;
            cache_res->m.lsq_entry_out  = req_info_r3.lsq_entry;
        }
        else {
            cache_res->m.valid_out      = true;
            cache_res->m.op_out         = OP_LD;
            cache_res->m.hit_out        = false;
            cache_res->m.miss_out       = mshr_alc_req->m.ok_in;
            cache_res->m.abort_out      = !mshr_alc_req->m.ok_in;
            cache_res->m.mshr_entry_out = mshr_alc_req->m.mshr_entry_in; 
            cache_res->m.lsq_entry_out  = req_info_r3.lsq_entry;
        }
    }
    else if (req_info_r3.valid && req_info_r3.op == OP_ST && !hit_info_r3.hit && !abort_w3) {
        if (!mshr_alc_req->m.ok_in) {
            cache_res->m.valid_out      = true;
            cache_res->m.op_out         = OP_ST;
            cache_res->m.hit_out        = false;
            cache_res->m.miss_out       = false;
            cache_res->m.abort_out      = true;
            cache_res->m.lsq_entry_out  = req_info_r3.lsq_entry;
        }
        else {
            cache_res->m.valid_out      = true;
            cache_res->m.op_out         = OP_ST;
            cache_res->m.hit_out        = false;
            cache_res->m.miss_out       = mshr_alc_req->m.ok_in;
            cache_res->m.abort_out      = !mshr_alc_req->m.ok_in;
            cache_res->m.mshr_entry_out = mshr_alc_req->m.mshr_entry_in; 
            cache_res->m.lsq_entry_out  = req_info_r3.lsq_entry;
        }
    }
}

void Dcache::seq() {
    req_info_r2 = req_info_r2_io;
    req_info_r3 = req_info_r3_io;
    fwd_info_r3 = fwd_info_r3_io;

    for (int way = 0; way < 2; way++) {
        if (tagv_ram_io[way].en && tagv_ram_io[way].we) {
            tagv_ram[way][tagv_ram_io[way].addr].v = tagv_ram_io[way].v;
            tagv_ram[way][tagv_ram_io[way].addr].tag = tagv_ram_io[way].tag;
        }
        for (int word = 0; word < 4; word++)
            for (int byte = 0; byte < 4; byte++)
            if (data_ram_io[way][word].en && data_ram_io[way][word].we[byte])
                data_ram[way][word][data_ram_io[way][word].addr].data[byte] = data_ram_io[way][word].din[byte];
    }

}