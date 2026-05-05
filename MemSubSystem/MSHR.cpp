#include "MSHR.h"
#include "PhysMemory.h"

#include <cassert>
#include <cstring>


// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void MSHR::init()
{
    std::memset(&cur, 0, sizeof(cur));
    std::memset(&nxt, 0, sizeof(nxt));
}
void MSHR::comb_outputs_dcache()
{   
    out.mshr2dcache->free = DCACHE_MSHR_ENTRIES - cur.mshr_count;

    for(int i=0;i<LSU_LDU_COUNT + LSU_STA_COUNT;i++){
        out.mshr2dcache->find_resp[i].valid = cur.find_hit[i];
    }

    if(cur.axi_resp_hold_valid){
        out.mshr2dcache->fill_req.valid = true;
        const MSHREntry &e = cur.mshr_entries[cur.axi_resp_hold_id];
        out.mshr2dcache->fill_req.addr = e.addr;
        std::memcpy(out.mshr2dcache->fill_req.data, cur.axi_resp_hold_data, DCACHE_WORD_NUM * sizeof(uint32_t));
    }
    else{
        out.mshr2dcache->fill_req.valid = false;
        out.mshr2dcache->fill_req.way_idx = 0;
        out.mshr2dcache->fill_req.addr = 0;
        std::memset(out.mshr2dcache->fill_req.data, 0, sizeof(out.mshr2dcache->fill_req.data));
    }
}
void MSHR::comb_outputs_axi()
{  
    out.axi_out->clear();

    for(uint32_t i=0; i<DCACHE_MSHR_ENTRIES; i++){
        const MSHREntry &ce = cur.mshr_entries[i];
        if (ce.valid && !ce.issued)
        {
            out.axi_out->req_valid = true;
            out.axi_out->req_addr = ce.addr;
            out.axi_out->req_total_size = DCACHE_LINE_SIZE - 1;
            out.axi_out->req_id = i;
            break;
        }
    }
    out.axi_out->resp_ready = !cur.axi_resp_hold_valid;
}

void MSHR::comb_inputs_dcache()
{
    for(int i=0;i<LSU_LDU_COUNT + LSU_STA_COUNT;i++){
        nxt.find_hit[i] = false;
        if(in.dcache2mshr->find_req[i].valid){
            for(int j=0;j<DCACHE_MSHR_ENTRIES;j++){
                const MSHREntry &e = cur.mshr_entries[j];
                uint32_t find_addr = get_addr(in.dcache2mshr->find_req[i].set_idx, in.dcache2mshr->find_req[i].tag, 0);
                if(e.valid && e.addr == find_addr){
                    nxt.find_hit[i] = true;
                    break;
                }
            }
        }
    }

    for(int i=0;i<DCACHE_MISS_NUM;i++){
        if(in.dcache2mshr->mshr_req[i].valid){
            const AddrFields f = decode(in.dcache2mshr->mshr_req[i].addr);
            const uint32_t line_addr = get_addr(f.set_idx, f.tag, 0);

            bool already_exists = false;
            for (int j = 0; j < DCACHE_MSHR_ENTRIES; j++) {
                if (nxt.mshr_entries[j].valid &&
                    nxt.mshr_entries[j].addr == line_addr) {
                    already_exists = true;
                    break;
                }
            }

            if (already_exists) {
                continue;
            }

            for(int j=0;j<DCACHE_MSHR_ENTRIES;j++){
                if(!nxt.mshr_entries[j].valid){
                    nxt.mshr_entries[j].valid = true;
                    nxt.mshr_entries[j].issued = false;
                    nxt.mshr_entries[j].addr = get_addr(f.set_idx, f.tag, 0);
                    nxt.mshr_count = nxt.mshr_count + 1;
                    break;
                }
            }
        }
    }

}
void MSHR::comb_inputs_axi()
{

    // ── Accept R channel response ─────────────────────────────────────────────

    if(!cur.axi_resp_hold_valid){
        if(in.axi_in->resp_valid){
            nxt.axi_resp_hold_valid = true;
            nxt.axi_resp_hold_id = in.axi_in->resp_id;
            std::memcpy(nxt.axi_resp_hold_data, in.axi_in->resp_data, DCACHE_WORD_NUM * sizeof(uint32_t));
            #if BSD_CONFIG
                Assert(in.axi_in->resp_id < DCACHE_MSHR_ENTRIES && "Invalid AXI response ID");
            #endif
        }
    }else{
        if(in.dcache2mshr->fill_resp.done){
            nxt.mshr_entries[cur.axi_resp_hold_id].valid = false;
            nxt.mshr_entries[cur.axi_resp_hold_id].issued = false;
            nxt.mshr_count = nxt.mshr_count - 1;
            nxt.axi_resp_hold_valid = false;
            nxt.axi_resp_hold_id = 0;
            std::memset(nxt.axi_resp_hold_data, 0, sizeof(nxt.axi_resp_hold_data));
            // Mark the MSHR entry as done, which will trigger the fill response to DCache and unblock the waiting load/store.
        }
    }

    // ── Issue next pending AR ─────────────────────────────────────────────────
    // Handshake note:
    // `req_ready` is only a ready-first hint. Use `req_accepted + req_accepted_id`
    // to mark exactly which MSHR slot completed AR handshake.
    if (in.axi_in->req_accepted) {
        const uint8_t acc_id = in.axi_in->req_accepted_id;
        if (acc_id < DCACHE_MSHR_ENTRIES) {
            const MSHREntry &ce = cur.mshr_entries[acc_id];
            if (ce.valid && !ce.issued) {
                nxt.mshr_entries[acc_id].issued = true;
            }
        }
        else
        {
            #if !BSD_CONFIG
                Assert(false && "Invalid MSHR response ID");
            #endif
        }
    }

}
void MSHR::seq()
{
    cur = nxt;
}

void MSHR::dump_debug_state(FILE *out) const {
    fprintf(out, "MSHR State:\n");
    fprintf(out, "  MSHR Count: %u\n", cur.mshr_count);
    fprintf(out, "  AXI Resp Hold Valid: %d, ID: %u\n", cur.axi_resp_hold_valid, cur.axi_resp_hold_id);
    for (uint32_t i = 0; i < DCACHE_MSHR_ENTRIES; i++) {
        const MSHREntry &e = cur.mshr_entries[i];
        fprintf(out, "  Entry %u: Valid: %d, Issued: %d, Addr: 0x%08x\n", i, e.valid, e.issued, e.addr);
    }
}
