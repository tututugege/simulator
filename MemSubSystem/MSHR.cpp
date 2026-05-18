#include "MSHR.h"
#include "PhysMemory.h"
#if !BSD_CONFIG
#include "types.h"
#endif

#include <cassert>
#include <cstring>

namespace {
int find_entry_idx(const MSHR_STATE &state, uint32_t line_addr) {
    for (int i = 0; i < DCACHE_MSHR_ENTRIES; i++) {
        const MSHREntry &e = state.mshr_entries[i];
        if (e.valid && e.addr == line_addr) {
            return i;
        }
    }
    return -1;
}

void clear_store_merge(MSHREntry &entry) {
    entry.merged_store_dirty = false;
    std::memset(entry.merged_store_data, 0, sizeof(entry.merged_store_data));
    std::memset(entry.merged_store_strb, 0, sizeof(entry.merged_store_strb));
}

void merge_store_into_entry(MSHREntry &entry, const MSHRReq &req) {
    if (!req.is_store) {
        return;
    }
    const AddrFields f = decode(req.addr);
    apply_strobe(entry.merged_store_data[f.word_off], req.data, req.strb);
    entry.merged_store_strb[f.word_off] |= req.strb;
    entry.merged_store_dirty = true;
}
} // namespace


// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────

void MSHR::init()
{
    std::memset(&cur, 0, sizeof(cur));
    std::memset(&nxt, 0, sizeof(nxt));
    axi_resp_accepted_this_cycle = false;
}
void MSHR::comb_outputs_dcache()
{   
    out.mshr2dcache->free = DCACHE_MSHR_ENTRIES - nxt.mshr_count;

    for(int i=0;i<LSU_LDU_COUNT + LSU_STA_COUNT;i++){
        bool hit = false;
        if (in.dcache2mshr->find_req[i].valid) {
            const uint32_t find_addr =
                get_addr(in.dcache2mshr->find_req[i].set_idx,
                         in.dcache2mshr->find_req[i].tag, 0);
            for (int j = 0; j < DCACHE_MSHR_ENTRIES; j++) {
                const MSHREntry &e = nxt.mshr_entries[j];
                if (e.valid && e.addr == find_addr) {
                    hit = true;
                    break;
                }
            }
        }
        nxt.find_hit[i] = hit;
        out.mshr2dcache->find_resp[i].valid = hit;
        out.mshr2dcache->find_resp[i].hit = hit;
    }

    if(nxt.axi_resp_hold_valid){
        out.mshr2dcache->fill_req.valid = true;
        const MSHREntry &e = nxt.mshr_entries[nxt.axi_resp_hold_id];
        out.mshr2dcache->fill_req.addr = e.addr;
        out.mshr2dcache->fill_req.dirty = e.merged_store_dirty;
#if !BSD_CONFIG
        out.mshr2dcache->fill_req.lsu_origin = e.lsu_origin;
#endif
        std::memcpy(out.mshr2dcache->fill_req.data, nxt.axi_resp_hold_data, DCACHE_WORD_NUM * sizeof(uint32_t));
        for (int w = 0; w < DCACHE_WORD_NUM; w++) {
            if (e.merged_store_strb[w] != 0) {
                apply_strobe(out.mshr2dcache->fill_req.data[w],
                             e.merged_store_data[w],
                             e.merged_store_strb[w]);
            }
        }
    }
    else{
        out.mshr2dcache->fill_req.valid = false;
        out.mshr2dcache->fill_req.dirty = false;
#if !BSD_CONFIG
        out.mshr2dcache->fill_req.lsu_origin = false;
#endif
        out.mshr2dcache->fill_req.way_idx = 0;
        out.mshr2dcache->fill_req.addr = 0;
        std::memset(out.mshr2dcache->fill_req.data, 0, sizeof(out.mshr2dcache->fill_req.data));
    }
}
void MSHR::comb_outputs_axi()
{  
    out.axi_out->clear();

    for(uint32_t i=0; i<DCACHE_MSHR_ENTRIES; i++){
        const MSHREntry &ce = nxt.mshr_entries[i];
        if (ce.valid && !ce.issued)
        {
            out.axi_out->req_valid = true;
            out.axi_out->req_addr = ce.addr;
            out.axi_out->req_total_size = DCACHE_LINE_SIZE - 1;
            out.axi_out->req_id = i;
            break;
        }
    }
    out.axi_out->resp_ready =
        in.axi_in->resp_valid ? axi_resp_accepted_this_cycle
                              : !nxt.axi_resp_hold_valid;
}

void MSHR::comb_inputs_dcache()
{
    if(in.dcache2mshr->fill_resp.done){
        const uint8_t hold_id = nxt.axi_resp_hold_id;
        Assert(hold_id < DCACHE_MSHR_ENTRIES &&
               "Invalid held MSHR response ID");
#if !BSD_CONFIG
        if (ctx != nullptr &&
            hold_id < DCACHE_MSHR_ENTRIES) {
            const MSHREntry &e = nxt.mshr_entries[hold_id];
            if (e.valid && e.lsu_origin &&
                ctx->perf.cycle >= e.alloc_cycle) {
                ctx->perf.l1d_miss_penalty_total_cycles +=
                    ctx->perf.cycle - e.alloc_cycle;
                ctx->perf.l1d_miss_penalty_samples++;
            }
        }
#endif
        nxt.mshr_entries[hold_id].valid = false;
        nxt.mshr_entries[hold_id].issued = false;
        clear_store_merge(nxt.mshr_entries[hold_id]);
#if !BSD_CONFIG
        nxt.mshr_entries[hold_id].alloc_cycle = 0;
        nxt.mshr_entries[hold_id].axi_read_start_cycle = 0;
        nxt.mshr_entries[hold_id].axi_read_active = false;
        nxt.mshr_entries[hold_id].lsu_origin = false;
#endif
        if (nxt.mshr_count > 0) {
            nxt.mshr_count = nxt.mshr_count - 1;
        }
        nxt.axi_resp_hold_valid = false;
        nxt.axi_resp_hold_id = 0;
        std::memset(nxt.axi_resp_hold_data, 0, sizeof(nxt.axi_resp_hold_data));
    }

    for(int i=0;i<LSU_LDU_COUNT + LSU_STA_COUNT;i++){
        nxt.find_hit[i] = false;
        if(in.dcache2mshr->find_req[i].valid){
            for(int j=0;j<DCACHE_MSHR_ENTRIES;j++){
                const MSHREntry &e = nxt.mshr_entries[j];
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

            int entry_idx = find_entry_idx(nxt, line_addr);

            if (entry_idx < 0) {
                for(int j=0;j<DCACHE_MSHR_ENTRIES;j++){
                    if(!nxt.mshr_entries[j].valid){
                        entry_idx = j;
                        nxt.mshr_entries[j].valid = true;
                        nxt.mshr_entries[j].issued = false;
                        nxt.mshr_entries[j].addr = line_addr;
                        clear_store_merge(nxt.mshr_entries[j]);
#if !BSD_CONFIG
                        nxt.mshr_entries[j].alloc_cycle =
                            ctx == nullptr ? 0 : ctx->perf.cycle;
                        nxt.mshr_entries[j].axi_read_start_cycle = 0;
                        nxt.mshr_entries[j].axi_read_active = false;
                        nxt.mshr_entries[j].lsu_origin =
                            in.dcache2mshr->mshr_req[i].lsu_origin;
                        if (ctx != nullptr &&
                            in.dcache2mshr->mshr_req[i].lsu_origin) {
                            ctx->perf.l1d_miss_mshr_alloc++;
                            ctx->perf.dcache_miss_num++;
                        }
#endif
                        nxt.mshr_count = nxt.mshr_count + 1;
                        break;
                    }
                }
            }

            if (entry_idx >= 0) {
                merge_store_into_entry(nxt.mshr_entries[entry_idx],
                                       in.dcache2mshr->mshr_req[i]);
            }
        }
    }

}
void MSHR::comb_inputs_axi()
{
    axi_resp_accepted_this_cycle = false;
    const bool req_accepted = in.axi_in->req_accepted;
    const uint8_t accepted_id = in.axi_in->req_accepted_id;

    // The AXI/LLC fabric can accept a DCache read and produce the response
    // before the next CPU cycle observes the issued bit. Make the accepted
    // state visible to response handling in this same comb pass.
    if (req_accepted) {
        if (accepted_id < DCACHE_MSHR_ENTRIES) {
            const MSHREntry &ce = cur.mshr_entries[accepted_id];
            if (ce.valid && !ce.issued) {
                nxt.mshr_entries[accepted_id].issued = true;
#if !BSD_CONFIG
                nxt.mshr_entries[accepted_id].axi_read_active = true;
                nxt.mshr_entries[accepted_id].axi_read_start_cycle =
                    ctx == nullptr ? 0 : ctx->perf.cycle;
#endif
            }
            if (out.axi_out->req_valid &&
                out.axi_out->req_id == accepted_id) {
                out.axi_out->req_valid = false;
                out.axi_out->req_addr = 0;
                out.axi_out->req_total_size = 0;
                out.axi_out->req_id = 0;
            }
        }
        else
        {
#if !BSD_CONFIG
            Assert(false && "Invalid MSHR accepted request ID");
#endif
        }
    }

    // ── Accept R channel response ─────────────────────────────────────────────

    if(!nxt.axi_resp_hold_valid){
        if(in.axi_in->resp_valid){
            const uint8_t resp_id = in.axi_in->resp_id;
            bool resp_matches_entry = false;
            if (resp_id < DCACHE_MSHR_ENTRIES) {
                const MSHREntry &e = nxt.mshr_entries[resp_id];
                resp_matches_entry = e.valid && e.issued;
            }
            if (resp_matches_entry) {
                nxt.axi_resp_hold_valid = true;
                nxt.axi_resp_hold_id = resp_id;
                std::memcpy(nxt.axi_resp_hold_data, in.axi_in->resp_data,
                            DCACHE_WORD_NUM * sizeof(uint32_t));
                axi_resp_accepted_this_cycle = true;
#if !BSD_CONFIG
                if (ctx != nullptr) {
                    const MSHREntry &e = nxt.mshr_entries[resp_id];
                    if (e.lsu_origin && e.axi_read_active &&
                        ctx->perf.cycle >= e.axi_read_start_cycle) {
                        ctx->perf.l1d_axi_read_total_cycles +=
                            ctx->perf.cycle - e.axi_read_start_cycle;
                        ctx->perf.l1d_axi_read_samples++;
                    }
                    nxt.mshr_entries[resp_id].axi_read_active = false;
                    nxt.mshr_entries[resp_id].axi_read_start_cycle = 0;
                }
#endif
            }
            else {
#if BSD_CONFIG
                Assert(false && "Invalid AXI response ID");
#endif
            }
        }
    }

}
void MSHR::seq()
{
    cur = nxt;
#if !BSD_CONFIG
    if (ctx != nullptr) {
        const uint64_t mshr_count = static_cast<uint64_t>(cur.mshr_count);
        ctx->perf.l1d_mshr_average_count += mshr_count;
        if (mshr_count > ctx->perf.l1d_mshr_max_count) {
            ctx->perf.l1d_mshr_max_count = mshr_count;
        }
    }
#endif
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
