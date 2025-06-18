#include "StoreQueue.h"
#include "AddrTransArb.h"
#include "Dcache.h"
#include "Mshr.h"
#include "MemUtil.h"
#include "frontend.h"

void Store_Queue::default_val() {
    for (int i = 0; i < 4; i++) {
        stq_io[i].valid = false;
        stq_io[i].fired = false;
        stq_io[i].done  = false;
        stq_io[i].miss  = false;
    }
}

void Store_Queue::alloc() {
    for (int i = 0; i < DECODE_WIDTH; i++) {
        if (stq_alloc[i].valid_in && (tail_r + 1) % 4 != head_r) {
            stq_io[tail_r].mem_sz = stq_alloc[i].mem_sz_in;
            for (int j = 0; j < 4; j++)
                stq_io[tail_r].wstrb[j]  = stq_alloc[i].wstrb_in[j]; 
            stq_alloc[i].ready_out = true;
            stq_alloc[i].stq_idx_out = tail_r;
            tail_r = (tail_r + 1) % 4;
        }
    }
    tail_r_io = tail_r;
}

void Store_Queue::free() {
    if (stq[head_r].valid && stq[head_r].done) {
        stq_io[head_r].valid = false;
        head_r_io = (head_r + 1) % 8;
    }
}

void Store_Queue::retire() {
    retire_r_io = (retire_r + *retire_num_in) % 8;
}

void Store_Queue::addr_trans_req_forepart() {
    for (int i = retire_r; i != tail_r; i = (i + 1) % 8)
        if (stq[i].valid && !stq[i].addr_trans) {
            stq_trans_req->m.valid_out = true;
            stq_trans_req->m.vtag_out  = stq[i].tag;
            stq_trans_req->m.stq_entry_out = i;
            return;
        }
}

void Store_Queue::addr_trans_req_backpart() {
    if (stq_trans_req->m.ready_in)
        stq[stq_trans_req->m.stq_entry_out].addr_trans = true;
}

void Store_Queue::fire_st2cache_forepart() {
    for (int i = head_r; i != retire_r; i = (i + 1) % 8)
        if (stq[i].valid && !stq[i].fired && stq[i].paddrv) {
            stq_cache_req->m.valid_out = true;
            stq_cache_req->m.op_out = OP_ST;
            stq_cache_req->m.tagv_out = true;
            stq_cache_req->m.tag_out = stq[i].tag;
            stq_cache_req->m.index_out = stq[i].index;
            stq_cache_req->m.word_out = stq[i].word;
            for (int byte = 0; byte < 4; byte++) {
                stq_cache_req->m.wdata_aft_sft_out[byte] = stq[i].wdata_aft_sft[byte];
                stq_cache_req->m.wstrb_out[byte] = stq[i].wstrb[byte];
            }
            stq_cache_req->m.lsq_entry_out = i;
            return;
        }
}

void Store_Queue::fire_st2cache_backpart() {
    if (stq_cache_req->m.addr_ok_in)
        stq_io[stq_cache_req->m.lsq_entry_out].fired = false;
}

void Store_Queue::fwd_handler() {
    for (int i = head_r; i != tail_r; i = (i + 1) % 8)
        if (stq[i].paddrv && stq[i].tag == stq_fwd_req->s.tag_in && stq[i].index == stq_fwd_req->s.index_in && stq[i].word == stq_fwd_req->s.word_in)
            for (int byte = 0; byte < 4; byte++)
                if (stq[i].wstrb[i] && stq_fwd_req->s.byte_mask_in[i]) {
                    stq_fwd_data->m.fwd_byte_out[byte] = stq[i].wdata_aft_sft[byte];
                    stq_fwd_data->m.fwd_strb_out[byte] = true;
                }
        else if (!stq[i].paddrv && stq_fill_req->s.valid_in && stq_fill_req->s.paddrv_in && stq_fill_req->s.tag_in == stq_fwd_req->s.tag_in && stq_fill_req->s.index_in == stq_fwd_req->s.index_in && stq_fill_req->s.word_in == stq_fwd_req->s.word_in)
            for (int byte = 0; byte < 4; byte++)
                if (stq_io[stq_fill_req->s.stq_entry_in].wstrb[i] && stq_fwd_req->s.byte_mask_in[i]) {
                    stq_fwd_data->m.fwd_byte_out[byte] = stq_io[stq_fill_req->s.stq_entry_in].wdata_aft_sft[byte];
                    stq_fwd_data->m.fwd_strb_out[byte] = true;
                }
}

void Store_Queue::fill_addr() {
    int stq_entry = stq_fill_req->s.stq_entry_in;
    if (stq_fill_req->s.valid_in) {
        stq_io[stq_entry].addr_trans = stq_fill_req->s.addr_trans_in;
        stq_io[stq_entry].paddrv     = stq_fill_req->s.paddrv_in;
        stq_io[stq_entry].tag        = stq_fill_req->s.tag_in;

        if (stq_fill_req->s.src_in == RS) {
            stq_io[stq_entry].tag = stq_fill_req->s.tag_in;
            stq_io[stq_entry].index = stq_fill_req->s.index_in;
            stq_io[stq_entry].word = stq_fill_req->s.word_in;
        
            if (stq_fill_req->s.offset_in == 0x0) {
                stq_io[stq_entry].wdata_aft_sft[0] = stq_fill_req->s.wdata_b4_sft_in[0];
                stq_io[stq_entry].wdata_aft_sft[1] = stq_fill_req->s.wdata_b4_sft_in[1];
                stq_io[stq_entry].wdata_aft_sft[2] = stq_fill_req->s.wdata_b4_sft_in[2];
                stq_io[stq_entry].wdata_aft_sft[3] = stq_fill_req->s.wdata_b4_sft_in[3];
            }
            else if (stq_fill_req->s.offset_in == 0x1) {
                stq_io[stq_entry].wdata_aft_sft[1] = stq_fill_req->s.wdata_b4_sft_in[0];
                stq_io[stq_entry].wdata_aft_sft[2] = stq_fill_req->s.wdata_b4_sft_in[1];
                stq_io[stq_entry].wdata_aft_sft[3] = stq_fill_req->s.wdata_b4_sft_in[2];
            }
            else if (stq_fill_req->s.offset_in == 0x2) {
                stq_io[stq_entry].wdata_aft_sft[2] = stq_fill_req->s.wdata_b4_sft_in[0];
                stq_io[stq_entry].wdata_aft_sft[3] = stq_fill_req->s.wdata_b4_sft_in[1];
            }
            else
                stq_io[stq_entry].wdata_aft_sft[3] = stq_fill_req->s.wdata_b4_sft_in[0];

            if (stq_fill_req->s.offset_in == 0x0)
                if (stq[stq_entry].mem_sz == BYTE)
                    stq_io[stq_entry].wstrb[0] = true;
                else if (stq[stq_entry].mem_sz == HALF) {
                    stq_io[stq_entry].wstrb[0] = true;
                    stq_io[stq_entry].wstrb[1] = true;
                }
                else {
                    stq_io[stq_entry].wstrb[0] = true;
                    stq_io[stq_entry].wstrb[1] = true;
                    stq_io[stq_entry].wstrb[2] = true;
                    stq_io[stq_entry].wstrb[3] = true;
                }
            else if (stq_fill_req->s.offset_in == 0x1)
                stq_io[stq_entry].wstrb[1] = true;
            else if (stq_fill_req->s.offset_in == 0x2)
                if (stq[stq_entry].mem_sz == BYTE) {
                    stq_io[stq_entry].wstrb[2] = true;
                    stq_io[stq_entry].wstrb[3] = true;
                }
            else if (stq_fill_req->s.offset_in == 0x3)
                stq_io[stq_entry].wstrb[3] = true;
        }
    }
}

void Store_Queue::recv_cache_res() {
    int stq_entry = cache_res->s.lsq_entry_in;
    if (cache_res->s.valid_in && cache_res->s.op_in == OP_ST) 
        if (cache_res->s.hit_in)
            stq_io[stq_entry].done = true;
        else if (cache_res->s.miss_in) {
            stq_io[stq_entry].miss = true;
            stq_io[stq_entry].mshr_entry = cache_res->s.mshr_entry_in;
        }
        else
            stq_io[stq_entry].fired = false;
}

void Store_Queue::recv_refill_data() {
    for (int i = head_r; i != retire_r; i = (i + 1) % 8)
        if (refill_bus->s.valid_in && stq[i].valid && stq[i].miss && refill_bus->s.mshr_entry_in == i) {
            stq_io[i].done = true;
            stq_io[i].miss = false;
        }
}

void Store_Queue::seq() {
    for (int i = 0; i < 8; i++) 
        stq[i] = stq_io[i];
    head_r   = head_r_io;
    retire_r = retire_r_io;
    tail_r   = tail_r_io; 
}