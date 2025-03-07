#include "StoreQueue.h"
#include "Dcache.h"

void Store_Queue::alloc() {
    bool stq_ful = (tail_r + 1) % 5 == head_r;
    if (ren2lsu->valid && ren2lsu->op == op_t::ST && !stq_ful) {
        lsu2ren->lsq_entry = tail_r;
        lsu2ren->ready = true;
    }
}

void Store_Queue::free() {
    if (stq[head_r].valid && stq[head_r].done) {
        stq_io[head_r].valid = false;
        head_r_io = (head_r + 1) % 5;
    }
}

void Store_Queue::retire() {
    if (!rob2stq->valid)
        return;
    retire_r_io = (retire_r + rob2stq->retire_num) % 5;
}

void Store_Queue::addr_trans_req_forepart() {
    for (int i = retire_r; i != tail_r; i = (i + 1) % 5)
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
            stq_cache_req->m.op_out = op_t::OP_ST;
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

// 增加访存指令年龄比较
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

        if (stq_fill_req->s.src_in == src_t::RS) {
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
                if (stq[stq_entry].mem_sz == mem_sz_t::BYTE)
                    stq_io[stq_entry].wstrb[0] = true;
                else if (stq[stq_entry].mem_sz == mem_sz_t::HALF) {
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
                if (stq[stq_entry].mem_sz == mem_sz_t::BYTE) {
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
    if (cache_res->s.valid_in && cache_res->s.op_in == op_t::OP_ST) 
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
    if (ren2lsu->valid && lsu2ren->ready && ren2lsu->dis_fire) {
        stq_io[tail_r].valid      = true;
        stq_io[tail_r].fired      = false;
        stq_io[tail_r].done       = false;
        stq_io[tail_r].miss       = false;
        stq_io[tail_r].addr_trans = false;
        stq_io[tail_r].paddrv     = false;
        stq_io[tail_r].mem_sz     = ren2lsu->mem_sz;
        stq_io[tail_r].id         = ren2lsu->id;
        tail_r_io = (tail_r + 1) % 9;
    }

    for (int i = 0; i < 8; i++) 
        stq[i] = stq_io[i];
    head_r   = head_r_io;
    retire_r = retire_r_io;
    tail_r   = tail_r_io; 
}