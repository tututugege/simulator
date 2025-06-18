#include "LoadQueue.h"
#include "StoreQueue.h"
#include "Dcache.h"
#include "Mshr.h"
#include "AddrTransArb.h"
#include "MemUtil.h"
#include "frontend.h"

void Load_Queue::default_val() {
    ldq_trans_req->m.valid_out = false;
    ldq_trans_req->m.vtag_out  = 0;
    ldq_trans_req->m.ldq_entry_out = 0;

    ldq_cache_req->m.valid_out = false;
    ldq_cache_req->m.op_out = OP_LD;
    ldq_cache_req->m.tagv_out = false;
    ldq_cache_req->m.tag_out = 0;
    ldq_cache_req->m.index_out = 0;
    ldq_cache_req->m.word_out = 0;
    ldq_cache_req->m.offset_out = 0;
    ldq_cache_req->m.lsq_entry_out = 0;
    for (int i = 0; i < 4; i++) {
        ldq_io[i].addr_trans = ldq[i].addr_trans;
        ldq_io[i].abort = false;
        ldq_io[i].addr_trans = false;
        ldq_io[i].paddrv = false;
        ldq_io[i].tag = 0;
        ldq_io[i].abort = false;
        ldq_io[i].index = 0;
        ldq_io[i].word = 0;
        ldq_io[i].offset = 0;
        for (int byte = 0; byte < 4; byte++)
            ldq_io[i].data_b4_bcast[byte] = 0;
        ldq_io[i].done = false;
        ldq_io[i].miss = false;
        ldq_io[i].abort = false;
        ldq_io[i].mshr_entry = 0;
    }
}

void Load_Queue::alloc() {
    for (int i = 0; i < DECODE_WIDTH; i++) {
        if (ldq_alloc[i].valid_in && (tail_r + 1) % 4 != head_r) {
            ldq_io[tail_r].valid    = true;
            ldq_io[tail_r].mem_sz   = ldq_alloc[i].mem_sz_in;
            ldq_io[tail_r].dst_preg = ldq_alloc[i].dst_preg_in;
            ldq_io[tail_r].sign     = ldq_alloc[i].sign_in;
            ldq_alloc[i].ready_out   = true;
            ldq_alloc[i].ldq_idx_out = tail_r;
            tail_r = (tail_r + 1) % 4;
        }
    }
    tail_r_io = tail_r;
}

void Load_Queue::free() {
    if (ldq[head_r].valid && ldq[head_r].done) {
        head_r_io = (head_r + 1) % 8;
        ldq_io[head_r].valid = false;
    }
}

void Load_Queue::addr_trans_req_forepart() {
    for (int i = head_r; i != tail_r; i = (i + 1) % 4)
        if (ldq[i].valid && !ldq[i].addr_trans) {
            ldq_trans_req->m.valid_out = true;
            ldq_trans_req->m.vtag_out  = ldq[i].tag;
            ldq_trans_req->m.ldq_entry_out = i;
            return;
        }
}

void Load_Queue::addr_trans_req_backpart() {
    if (ldq_trans_req->m.ready_in)
        ldq_io[ldq_trans_req->m.ldq_entry_out].addr_trans = true;
}

void Load_Queue::fire_ld2cache_forepart() {
    for (int i = head_r; i != tail_r; i = (i + 1) % 4)
        if (ldq[i].valid && ldq[i].abort && ldq[i].paddrv) {
            ldq_cache_req->m.valid_out = true;
            ldq_cache_req->m.op_out = OP_LD;
            ldq_cache_req->m.tagv_out = true;
            ldq_cache_req->m.tag_out = ldq[i].tag;
            ldq_cache_req->m.index_out = ldq[i].index;
            ldq_cache_req->m.word_out = ldq[i].word;
            ldq_cache_req->m.offset_out = ldq[i].offset;
            ldq_cache_req->m.lsq_entry_out = i;
            return;
        }
}

void Load_Queue::fire_ld2cache_backpart() {
    if (ldq_cache_req->m.addr_ok_in)
        ldq_io[ldq_cache_req->m.lsq_entry_out].abort = false;
}

void Load_Queue::fill_addr() {
    int ldq_entry = ldq_fill_req->s.ldq_entry_in;
    if (ldq_fill_req->s.valid_in) {
        ldq_io[ldq_entry].addr_trans = ldq_fill_req->s.addr_trans_in;
        ldq_io[ldq_entry].paddrv     = ldq_fill_req->s.paddrv_in;
        ldq_io[ldq_entry].tag        = ldq_fill_req->s.tag_in;
        ldq_io[ldq_entry].abort      = false;
        if (ldq_fill_req->s.src_in == RS) {
            ldq_io[ldq_entry].index      = ldq_fill_req->s.index_in;
            ldq_io[ldq_entry].word       = ldq_fill_req->s.word_in;
            ldq_io[ldq_entry].offset     = ldq_fill_req->s.offset_in;
        }
    }
}

void Load_Queue::fwd_req() {
    stq_fwd_req->m.tag_out   = ldq_fill_req->s.tag_in;
    stq_fwd_req->m.index_out = ldq_fill_req->s.index_in;
    stq_fwd_req->m.word_out  = ldq_fill_req->s.word_in;

    bool byte_mask[4] = {false, false, false, false};
    if (ldq_fill_req->s.offset_in == 0x0) {
        byte_mask[0] = byte_mask[1] = byte_mask[2] = byte_mask[3] = true;
    }
    else if (ldq_fill_req->s.offset_in == 0x1)
        byte_mask[1] = true;
    else if (ldq_fill_req->s.offset_in == 0x2) {
        byte_mask[2] = byte_mask[3] = true;
    }
    else
        byte_mask[3] = true;

    for (int byte = 0; byte < 4; byte++) 
        stq_fwd_req->m.byte_mask_out[byte] = byte_mask[byte];
}

void Load_Queue::fill_fwd_data() {
    if (ldq_fill_req->s.valid_in) {
        int ldq_entry = ldq_fill_req->s.ldq_entry_in;
        for (int byte = 0; byte < 4; byte++)
           ldq_io[ldq_entry].data_b4_bcast[byte] = stq_fwd_data->s.fwd_byte_in[byte];
    }
}

void Load_Queue::recv_cache_res() {
    int ldq_entry = cache_res->s.lsq_entry_in;
    if (cache_res->s.valid_in && cache_res->s.op_in == OP_LD) {
        if (cache_res->s.hit_in) {
            ldq_io[ldq_entry].done = true;
            for (int byte = 0; byte < 4; byte++)
                ldq_io[ldq_entry].data_b4_bcast[byte] = cache_res->s.data_in[byte];
        }
        else if (cache_res->s.miss_in) {
            ldq_io[ldq_entry].miss = true;
            ldq_io[ldq_entry].mshr_entry = cache_res->s.mshr_entry_in;
        }
        else
            ldq_io[ldq_entry].abort = true;
    }
}

void Load_Queue::recv_refill_data() {
    for (int i = head_r; i != tail_r; i = (i + 1) % 4)
        if (refill_bus->s.valid_in && ldq[i].valid && ldq[i].miss && refill_bus->s.mshr_entry_in == i) {
            for (int byte = 0; byte < 4; byte++)
                ldq_io[i].data_b4_bcast[byte] = refill_bus->s.refill_data_in[ldq[i].word][byte];
            ldq_io[i].done = true;
            ldq_io[i].miss = false;
            ldq_io[i].abort = false;
        }
}

void Load_Queue::bcast_res_bus() {
    for (int i = head_r; i != tail_r; i = (i + 1) % 8) {
        if (ldq[i].valid && ldq[i].done && !ldq[i].bcast) {
            bcast_bus->valid_out = true;
            bcast_bus->dst_preg_out = ldq[i].dst_preg;

            if (ldq[i].offset == 0x0) {
                if (ldq[i].mem_sz == BYTE)  {
                    if (ldq[i].sign && ldq[i].data_b4_bcast[0] > 0x8)
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0xf;
                    else
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0x0;
                }
                else if (ldq[i].mem_sz == HALF) {
                    if (ldq[i].sign && ldq[i].data_b4_bcast[1] > 0x8)
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = 0xf;
                    else
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = 0x0;
                }
            }
            else if (ldq[i].offset == 0x1) {
                bcast_bus->data_out[0] = ldq[i].data_b4_bcast[1];
                if (ldq[i].sign && bcast_bus->data_out[0] > 0x8)
                    bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0xf;
                else
                    bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0x0;
            }
            else if (ldq[i].offset == 0x2) {
                bcast_bus->data_out[0] = ldq[i].data_b4_bcast[2];
                bcast_bus->data_out[1] = ldq[i].data_b4_bcast[3];
                if (ldq[i].mem_sz == BYTE) {
                    if (ldq[i].sign && bcast_bus->data_out[0] > 0x8)
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0xf;
                    else
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0x0;
                }
                else if (ldq[i].mem_sz == HALF) {
                    if (ldq[i].sign && bcast_bus->data_out[1] > 0x8)
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = 0xf;
                    else
                        bcast_bus->data_out[3] = bcast_bus->data_out[2] = 0x0;
                }
            }
            else if (ldq[i].offset == 0x3) {
                bcast_bus->data_out[0] = ldq[i].data_b4_bcast[3];
                if (ldq[i].sign && bcast_bus->data_out[0] > 0x8)
                    bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0xf;
                else
                    bcast_bus->data_out[3] = bcast_bus->data_out[2] = bcast_bus->data_out[1] = 0x0;
            }
        }
        return;
    }
}

void Load_Queue::seq() {
    for (int i = 0; i < 8; i++)
        ldq[i] = ldq_io[i];
    head_r = head_r_io;
    tail_r = tail_r_io;
}