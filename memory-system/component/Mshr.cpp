#include "Mshr.h"
#include "Memory.h"
#include "Dcache.h"

void Mshr::default_val() {
    mshr_alc_req->s.ok_out = false;
    mshr_alc_req->s.mshr_entry_out = 0;
    for (int i = 0; i < 4; i++) {
        mshr_r_io[i].valid = mshr_r[i].valid;
        mshr_r_io[i].fetch = mshr_r[i].fetch;
        mshr_r_io[i].fetching = mshr_r[i].fetching;
        mshr_r_io[i].fetched = mshr_r[i].fetched;
        mshr_r_io[i].repl_issued = mshr_r[i].repl_issued;
        mshr_r_io[i].wb2mem = mshr_r[i].wb2mem;
        mshr_r_io[i].wb2meming = mshr_r[i].wb2meming;
        mshr_r_io[i].wb2mem_ok = mshr_r[i].wb2mem_ok;
        mshr_r_io[i].req_tag = mshr_r[i].req_tag;
        mshr_r_io[i].req_idx = mshr_r[i].req_idx;
        mshr_r_io[i].cnt = mshr_r[i].cnt;
    }

    axi_ar->m.arvalid_out = false;
    axi_ar->m.arid_out    = 0;
    axi_ar->m.araddr_out  = 0;
    axi_ar->m.arlen_out   = 0;
    axi_ar->m.arsize_out  = 0;
}

void Mshr::alloc() {
    int empty_entry = -1;
    if (mshr_alc_req->s.valid_in) {
        for (int i = 0; i < 4; i++)
            if (mshr_r[i].valid && mshr_alc_req->s.tag_in == mshr_r[i].req_tag && mshr_alc_req->s.index_in == mshr_r[i].req_idx && !mshr_r[i].repl_issued) {
                mshr_alc_req->s.ok_out = true;
                mshr_alc_req->s.mshr_entry_out = i;
                return;
            }
            else if (!mshr_r[i].valid && empty_entry == -1)
                empty_entry = i;
        
        if (empty_entry == -1)
            mshr_alc_req->s.ok_out = false;
        else {
            mshr_alc_req->s.ok_out = true;
            mshr_alc_req->s.mshr_entry_out = empty_entry;
            mshr_r_io[empty_entry].valid = true;
            mshr_r_io[empty_entry].fetch = true;
            mshr_r_io[empty_entry].fetching = false;
            mshr_r_io[empty_entry].fetched = false;
            mshr_r_io[empty_entry].repl_issued = false;
            mshr_r_io[empty_entry].wb2mem = false;
            mshr_r_io[empty_entry].wb2meming = false;
            mshr_r_io[empty_entry].wb2mem_ok = false;
            mshr_r_io[empty_entry].req_tag = mshr_alc_req->s.tag_in;
            mshr_r_io[empty_entry].req_idx = mshr_alc_req->s.index_in;
            mshr_r_io[empty_entry].cnt = 0;
        }
    }
}

void Mshr::free() { 
    for (int i = 0; i < 4; i++)
        if (mshr_r[i].valid && mshr_r[i].wb2mem_ok) {
            mshr_r_io[i].valid = false;
            return;
        }
}

bool Mshr::raw_check(int entry) {
    for (int i = 0; i < 4; i++)
        if (mshr_r[i].wb2mem && !mshr_r[i].wb2mem_ok && mshr_r[i].repl_tag == mshr_r[entry].req_tag && mshr_r[i].req_idx == mshr_r[entry].req_idx)
            return false;
        else
            return true;
}

void Mshr::fetch_forepart() {
    fetch_ent = 0;
    for (int i = 0; i < 4; i++)
        if (mshr_r[i].valid && mshr_r[i].fetch && !mshr_r[i].fetching && raw_check(i)) {
            axi_ar->m.arvalid_out = true;
            axi_ar->m.arid_out    = i;
            axi_ar->m.araddr_out  = (mshr_r[i].req_tag << 20) + (mshr_r[i].req_idx << 4);
            axi_ar->m.arlen_out   = 0x3;
            axi_ar->m.arsize_out  = 0x2;
            fetch_ent = i;
            return;
        }
} 

void Mshr::fetch_backpart() {
    if (axi_ar->m.arready_in)
        mshr_r_io[fetch_ent].fetching = true;
}

void Mshr::axi_r_func() {
    int mshr_entry;
    axi_r->s.rready_out = true;
    memory->r_func();
    mshr_entry = axi_r->s.rid_in;
    axi_r_req.mshr_entry = mshr_entry;
    if (axi_r->s.rvalid_in) {
        axi_r_req.word = mshr_r[mshr_entry].cnt;
        for (int byte = 0; byte < 4; byte++)
            axi_r_req.data[byte] = axi_r->s.rdata_in[byte];
        mshr_r_io[mshr_entry].cnt = mshr_r[mshr_entry].cnt + 1;
        if (axi_r->s.rlast_in)
            mshr_r_io[mshr_entry].fetched = true;
    }
}

void Mshr::replace() {
    if (!repl_r)
        for (int i = 0; i < 4; i++)
            if (mshr_r[i].valid && mshr_r[i].fetched && !mshr_r[i].repl_issued) {
                int repl_way = 0;
                dcache->tagv_ram_io[repl_way].en = true;
                dcache->tagv_ram_io[repl_way].addr = mshr_r[i].req_idx;

                for (int word = 0; word < 4; word++) {
                    dcache->data_ram_io[repl_way][word].en = true;
                    dcache->data_ram_io[repl_way][word].addr = mshr_r[i].req_idx;
                }
                repl_r_io = true;
                repl_entry_r_io = i;
                mshr_r_io[i].repl_issued = true;
                mshr_r_io[i].repl_way = repl_way;
                mshr_info->m.cache_replace_out = true;
                return;
            }
}

void Mshr::refill() {
    if (repl_r) {
        int repl_way = mshr_r[repl_entry_r].repl_way;
        int repl_idx = mshr_r[repl_entry_r].req_idx;
        for (int word = 0; word < 4; word++)
            for (int offset = 0; offset < 4; offset++)
                mshr_r_io[repl_entry_r].repl_data[word][offset] = dcache->data_ram[repl_way][repl_idx][word].data[offset];
        mshr_r_io[repl_entry_r].repl_tag = dcache->tagv_ram[repl_way][repl_idx].tag;
        if (dcache->tagv_ram[repl_way][repl_idx].v && dcache->dirty_r[repl_way][repl_idx])
            mshr_r_io[repl_entry_r].wb2mem = true;
        else {
            mshr_r_io[repl_entry_r].wb2mem = true;
            mshr_r_io[repl_entry_r].wb2mem_ok = true;
        }

        refill_bus->m.valid_out = true;
        refill_bus->m.mshr_entry_out = repl_entry_r;
        for (int word = 0; word < 4; word++)
            for (int offset = 0; offset < 4; offset++)
                refill_bus->m.refill_data_out[word][offset] = mshr_r[repl_entry_r].data2cache[word][offset];

        dcache->tagv_ram_io[repl_way].en   = true;
        dcache->tagv_ram_io[repl_way].we   = true;
        dcache->tagv_ram_io[repl_way].addr = repl_idx;
        dcache->tagv_ram_io[repl_way].v    = true;
        dcache->tagv_ram_io[repl_way].tag  = mshr_r[repl_entry_r].req_tag;

        repl_r_io = false;
        mshr_info->m.cache_refill_out = true;
    }
}

void Mshr::wb2mem() {
    for (int i = 0; i < 4; i++)
        if (mshr_r[i].valid && mshr_r[i].wb2mem && !mshr_r[i].wb2meming) {
            axi_aw->m.awid_out = i;
            axi_aw->m.awaddr_out = mshr_r[i].repl_tag << 12 + mshr_r[i].req_idx << 4;
            for (int byte = 0; byte < 16; byte++)
                axi_aw->m.wdata_out[byte] = mshr_r[i].repl_data[byte / 4][ byte % 4];
            axi_aw->m.awvalid_out = true;
            memory->aw_func();
            if (axi_aw->m.awready_in)
                mshr_r_io[i].wb2meming = true;
            return;
        }
}

void Mshr::wb2mem_ok() {
    axi_b->s.bready_out = true;
    memory->b_func();
    if (axi_b->s.bvalid_in)
        mshr_r_io[axi_b->s.bid_in].wb2mem_ok = true;
}

void Mshr::update_data2cache() {
    if (mshr_alc_req->s.valid_in  && axi_r->s.rvalid_in) {
        if (mshr_alc_req->s.word_in == axi_r_req.word && mshr_alc_req->s.mshr_entry_out == axi_r_req.mshr_entry) {
            for (int byte = 0; byte < 4; byte++) {
                if (mshr_alc_req->s.wdata_strb_in[byte]) {
                    mshr_r_io[mshr_alc_req->s.mshr_entry_out].data2cache[mshr_alc_req->s.word_in][byte] = mshr_alc_req->s.wdata_aft_sft_in[byte];
                    mshr_r_io[mshr_alc_req->s.mshr_entry_out].strb2cache[mshr_alc_req->s.word_in][byte] = true;
                }
                else if (!mshr_r[axi_r_req.mshr_entry].strb2cache[axi_r_req.word][byte])
                    mshr_r_io[axi_r_req.mshr_entry].data2cache[axi_r_req.word][byte] = axi_r_req.data[byte];
            }
        }
        else {
            for (int byte = 0; byte < 4; byte++) {
                if (mshr_alc_req->s.wdata_strb_in[byte]) {
                    mshr_r_io[mshr_alc_req->s.mshr_entry_out].data2cache[mshr_alc_req->s.word_in][byte] = mshr_alc_req->s.wdata_aft_sft_in[byte];
                    mshr_r_io[mshr_alc_req->s.mshr_entry_out].strb2cache[mshr_alc_req->s.word_in][byte] = true; 
                }
                if (!mshr_r[axi_r_req.mshr_entry].strb2cache[axi_r_req.word][byte])
                    mshr_r_io[axi_r_req.mshr_entry].data2cache[axi_r_req.word][byte] = axi_r_req.data[byte];
            }
        }
    }
    else if (mshr_alc_req->s.valid_in) {
        for (int byte = 0; byte < 4; byte++)
            if (mshr_alc_req->s.wdata_strb_in[byte]) {
                mshr_r_io[mshr_alc_req->s.mshr_entry_out].data2cache[mshr_alc_req->s.word_in][byte] = mshr_alc_req->s.wdata_aft_sft_in[byte];
                mshr_r_io[mshr_alc_req->s.mshr_entry_out].strb2cache[mshr_alc_req->s.word_in][byte] = true;
            }
    }
    else if (axi_r->s.rvalid_in) {
        for (int byte = 0; byte < 4; byte++)
        if (!mshr_r[axi_r_req.mshr_entry].strb2cache[axi_r_req.word][byte])
            mshr_r_io[axi_r_req.mshr_entry].data2cache[axi_r_req.word][byte] = axi_r_req.data[byte];
    }
}