#include "Memory.h"

void Memory::ar_func() {
    bool ar_buf_ful = (ar_tail + 1) % 5 == ar_head;
    if (ar_stat == stat_t::INIT && axi_ar->s.arvalid_in && !ar_buf_ful) {
        ar_stat = stat_t::WAIT;
        arready_cycle = cycle + 3;
        ar_buf[ar_tail].addr  = axi_ar->s.araddr_in;
        ar_buf[ar_tail].arlen = axi_ar->s.arlen_in;
        ar_buf[ar_tail].trans_cycle = cycle + 103;
        ar_buf[ar_tail].len = 0;
        ar_buf[ar_tail].id = axi_ar->s.arid_in;
        ar_tail = (ar_tail + 1) % 5;
    }
    else if (ar_stat == stat_t::WAIT && arready_cycle == cycle) {
        ar_stat = stat_t::INIT;
        axi_ar->s.arready_out = true;
    }
}

void Memory::r_func() {
    bool ar_buf_ept = ar_head == ar_tail;
    if (ar_buf_ept)
        return;
    if (cycle == ar_buf[ar_head].trans_cycle + ar_buf[ar_head].len * 2) {
        axi_r->m.rvalid_out = true;
        axi_r->m.rid_out = ar_buf[ar_head].id;
        for (int byte = 0; byte < 4; byte++)
            axi_r->m.rdata_out[byte] = mem[ar_buf[ar_head].addr + 4*ar_buf[ar_head].len + byte];
        ar_buf[ar_head].len++;
        if (ar_buf[ar_head].len == ar_buf[ar_head].arlen) {
            ar_head = (ar_head + 1) % 5;
            axi_r->m.rlast_out = true;
        } 
    }
}

void Memory::aw_func() {
    bool aw_buf_ful = (aw_tail + 1) % 5 == aw_head;
    if (aw_stat == stat_t::INIT && axi_aw->s.awvalid_in && !aw_buf_ful) {
        aw_stat = stat_t::WAIT;
        awready_cycle = cycle + 3;
        aw_buf[aw_tail].addr = axi_aw->s.awaddr_in;
        aw_buf[aw_tail].bvld_cycle = cycle + 9;
        aw_buf[aw_tail].id = axi_aw->s.awid_in;
        for (int byte = 0; byte < 16; byte++)
            aw_buf[aw_tail].data[byte] = axi_aw->s.wdata_in[byte];
        aw_tail = (aw_tail + 1) % 5;
    }
    else if (aw_stat == stat_t::WAIT && cycle == awready_cycle) {
        aw_stat = stat_t::INIT;
        axi_aw->s.awready_out = true;
    }
}

void Memory::b_func() {
    bool aw_buf_ept = aw_head == aw_tail;
    if (aw_buf_ept)
        return;
    if (aw_buf[aw_head].bvld_cycle) {
        axi_b->m.bvalid_out = true;
        axi_b->m.bid_out = aw_buf[aw_head].id;
        for (int byte = 0; byte < 16; byte++)
            mem[aw_buf[aw_head].addr + byte] = aw_buf[aw_head].data[byte];
        aw_head = (aw_head + 1) % 5;
    }
}

void Memory::seq() {
    cycle++;
}
