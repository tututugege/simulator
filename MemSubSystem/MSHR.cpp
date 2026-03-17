#include "MSHR.h"
#include "DeadlockReplayTrace.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern uint32_t *p_memory;

MSHREntry mshr_entries_nxt[MSHR_ENTRIES];

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void MSHR::init()
{
    std::memset(&cur, 0, sizeof(cur));
    std::memset(&nxt, 0, sizeof(nxt));
    std::memset(mshr_entries_nxt, 0, sizeof(mshr_entries_nxt));
    std::memset(miss_alloc_cycle, 0, sizeof(miss_alloc_cycle));
    std::memset(miss_alloc_cycle_valid, 0, sizeof(miss_alloc_cycle_valid));
    std::memset(axi_issue_cycle, 0, sizeof(axi_issue_cycle));
    std::memset(axi_issue_cycle_valid, 0, sizeof(axi_issue_cycle_valid));

    std::memset(&out, 0, sizeof(out));
}

// ─────────────────────────────────────────────────────────────────────────────
// comb_outputs — Phase 1: compute lookups, full, and fill delivery from cur.
//
// Must be called BEFORE stage2_comb() so that lookup results and full flag
// are stable when stage2_comb() decides whether to alloc or add_secondary.
// ─────────────────────────────────────────────────────────────────────────────
void MSHR::comb_outputs()
{   
    out.mshr2dcache.free = MSHR_ENTRIES - cur.mshr_count;
    out.replay_resp.replay = cur.fill; // MSHR full replay code
    out.replay_resp.replay_addr = cur.fill_addr;
    out.replay_resp.free_slots = out.mshr2dcache.free;
    if (out.replay_resp.replay)
    {
        deadlock_replay_trace::record(
            DeadlockReplayTraceKind::MshrBroadcast, 0,
            static_cast<uint8_t>(out.replay_resp.replay), 0,
            static_cast<uint8_t>(cur.fill_valid),
            static_cast<uint8_t>(cur.fill_way),
            0, 0, static_cast<uint32_t>(out.replay_resp.replay_addr),
            static_cast<uint32_t>(out.replay_resp.free_slots),
            static_cast<uint32_t>(cur.mshr_count));
    }

    // Registered fill output (from previous cycle comb_inputs()).
    out.mshr2dcache.fill.valid = cur.fill_valid;
    out.mshr2dcache.fill.way = cur.fill_way;
    out.mshr2dcache.fill.addr = cur.fill_addr;
    std::memcpy(out.mshr2dcache.fill.data, cur.fill_data,
                sizeof(out.mshr2dcache.fill.data));

    // Registered WB-eviction output (from previous cycle comb_inputs()).
    out.mshrwb.valid = cur.wb_valid;
    out.mshrwb.addr = cur.wb_addr;
    std::memcpy(out.mshrwb.data, cur.wb_data, sizeof(out.mshrwb.data));

    // AXI outputs.
    out.axi_out.req_valid = false;
    out.axi_out.req_addr = 0;
    out.axi_out.req_total_size = 0;
    out.axi_out.req_id = 0;
    // Default to ready; may be deasserted when WB backpressure prevents
    // consuming the current-cycle AXI read response.
    out.axi_out.resp_ready = true;

    if (in.axi_in.resp_valid) {
        const uint8_t resp_id = in.axi_in.resp_id;
        if (resp_id < MSHR_ENTRIES) {
            const MSHREntry &e = mshr_entries[resp_id];
            if (e.valid && e.issued && !e.fill) {
                const uint32_t lru_idx = choose_lru_victim(e.index);
                const bool need_wb_evict = dirty_array[e.index][lru_idx];
                if (need_wb_evict && !in.wbmshr.ready) {
                    out.axi_out.resp_ready = false;
                }
            }
        }
    }

    for(int i=0; i<MSHR_ENTRIES; i++){
        const MSHREntry &ce = mshr_entries[i];
        if (!ce.valid || ce.issued)
            continue;

        out.axi_out.req_valid = true;
        out.axi_out.req_addr = get_addr(ce.index, ce.tag, 0);
        out.axi_out.req_total_size = 31;
        out.axi_out.req_id = static_cast<uint8_t>(i);
        break;
    }


}

int MSHR::entries_add(int set_idx, int tag)
{   
    if(nxt.mshr_count >= MSHR_ENTRIES){
        Assert(0 && "MSHR full");
    }
    int alloc_idx = -1;
    for (int off = 0; off < MSHR_ENTRIES; off++)
    {
        if (!mshr_entries_nxt[off].valid)
        {
            alloc_idx = off;
            break;
        }
    }
    Assert(alloc_idx >= 0 && "MSHR full but count says not full");
    mshr_entries_nxt[alloc_idx].valid = true;
    mshr_entries_nxt[alloc_idx].issued = false;
    mshr_entries_nxt[alloc_idx].fill = false;
    mshr_entries_nxt[alloc_idx].index = set_idx;
    mshr_entries_nxt[alloc_idx].tag = tag;
    nxt.mshr_count++;
    miss_alloc_cycle[alloc_idx] = static_cast<uint64_t>(sim_time);
    miss_alloc_cycle_valid[alloc_idx] = true;
    axi_issue_cycle[alloc_idx] = 0;
    axi_issue_cycle_valid[alloc_idx] = false;
    DBG_PRINTF("[MSHR ALLOC] cyc=%lld idx=%d line=0x%08x count=%u\n",
               (long long)sim_time, alloc_idx,
               get_addr(set_idx, tag, 0), nxt.mshr_count);
    return alloc_idx;
}

void MSHR::comb_inputs()
{
    // One-cycle pulse/state outputs are generated into nxt and exposed by
    // comb_outputs() in the next cycle.
    nxt.fill = false;
    nxt.fill_valid = false;
    nxt.fill_way = 0;
    std::memset(nxt.fill_data, 0, sizeof(nxt.fill_data));
    nxt.wb_valid = false;
    nxt.wb_addr = 0;
    std::memset(nxt.wb_data, 0, sizeof(nxt.wb_data));

    // ── Process alloc and secondary requests ─────────────────────────────────
    for (int i = 0; i < LSU_LDU_COUNT; i++)
    {
        const LoadReq &req = in.dcachemshr.load_reqs[i];
        if (!req.valid)
            continue;
        entries_add(decode(req.addr).set_idx, decode(req.addr).tag);
    }

    for (int i = 0; i < LSU_STA_COUNT; i++)
    {
        const StoreReq &req = in.dcachemshr.store_reqs[i];
        if (!req.valid)
            continue;
        entries_add(decode(req.addr).set_idx, decode(req.addr).tag);
    }

    // ── Accept R channel response ─────────────────────────────────────────────
    if (in.axi_in.resp_valid)
    {
        uint8_t resp_id = in.axi_in.resp_id;
        if (resp_id < MSHR_ENTRIES)
        {
            const MSHREntry &e = mshr_entries[resp_id];
            if (e.valid && e.issued && !e.fill)
            {
                uint32_t lru_idx = choose_lru_victim(mshr_entries[resp_id].index);
                bool need_wb_evict = dirty_array[mshr_entries[resp_id].index][lru_idx];
                bool can_consume_resp = (!need_wb_evict) || in.wbmshr.ready;
                if (!can_consume_resp)
                {
                    DBG_PRINTF("[MSHR RESP HOLD] cyc=%lld resp_id=%u line=0x%08x need_wb=%d wb_ready=%d wb_count=%u\n",
                               (long long)sim_time, (unsigned)resp_id,
                               get_addr(mshr_entries[resp_id].index, mshr_entries[resp_id].tag, 0),
                               (int)need_wb_evict, (int)in.wbmshr.ready, cur.mshr_count);
                }
                else
                {
                    const uint32_t fill_line_addr =
                        get_addr(mshr_entries[resp_id].index, mshr_entries[resp_id].tag, 0);
                    if (need_wb_evict)
                    {
                        nxt.wb_valid = true;
                        nxt.wb_addr = get_addr(mshr_entries[resp_id].index, tag_array[mshr_entries[resp_id].index][lru_idx], 0);
                        for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                        {
                            nxt.wb_data[w] = data_array[mshr_entries[resp_id].index][lru_idx][w];
                        }
                        // Preserve same-cycle store-hit updates that have not
                        // reached data_array yet (committed in RealDcache::seq()).
                        // Apply in store-port order to match seq() semantics.
                        for (int p = 0; p < LSU_STA_COUNT; p++)
                        {
                            const auto &u = in.dcachemshr.store_hit_updates[p];
                            if (!u.valid)
                            {
                                continue;
                            }
                            if (u.set_idx != mshr_entries[resp_id].index)
                            {
                                continue;
                            }
                            if (u.way_idx != lru_idx)
                            {
                                continue;
                            }
                            if (u.word_off >= DCACHE_LINE_WORDS)
                            {
                                continue;
                            }
                            apply_strobe(nxt.wb_data[u.word_off], u.data, u.strb);
                        }
                        DBG_PRINTF("[MSHR WB] cyc=%lld resp_id=%u evict_line=0x%08x set=%u way=%u data=[%08x %08x %08x %08x %08x %08x %08x %08x]\n",
                                   (long long)sim_time, (unsigned)resp_id,
                                   nxt.wb_addr, mshr_entries[resp_id].index, lru_idx,
                                   nxt.wb_data[0], nxt.wb_data[1], nxt.wb_data[2], nxt.wb_data[3],
                                   nxt.wb_data[4], nxt.wb_data[5], nxt.wb_data[6], nxt.wb_data[7]);
                    }
                    if (ctx != nullptr)
                    {
                        const uint64_t now = static_cast<uint64_t>(sim_time);
                        if (miss_alloc_cycle_valid[resp_id] && now >= miss_alloc_cycle[resp_id])
                        {
                            ctx->perf.l1d_miss_penalty_total_cycles +=
                                (now - miss_alloc_cycle[resp_id]);
                            ctx->perf.l1d_miss_penalty_samples++;
                        }
                        if (axi_issue_cycle_valid[resp_id] && now >= axi_issue_cycle[resp_id])
                        {
                            ctx->perf.l1d_axi_read_total_cycles +=
                                (now - axi_issue_cycle[resp_id]);
                            ctx->perf.l1d_axi_read_samples++;
                        }
                    }
                    miss_alloc_cycle_valid[resp_id] = false;
                    axi_issue_cycle_valid[resp_id] = false;
                    nxt.fill_valid = true;
                    nxt.fill_way = lru_idx;
                    nxt.fill_addr = fill_line_addr;
                    for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                    {
                        nxt.fill_data[w] = in.axi_in.resp_data[w];
                    }

                    DBG_PRINTF("[MSHR FILL] cyc=%lld resp_id=%u line=0x%08x set=%u way=%u need_wb=%d data=[%08x %08x %08x %08x %08x %08x %08x %08x]\n",
                               (long long)sim_time, (unsigned)resp_id,
                               fill_line_addr, mshr_entries[resp_id].index, lru_idx, (int)need_wb_evict,
                               nxt.fill_data[0], nxt.fill_data[1], nxt.fill_data[2], nxt.fill_data[3],
                               nxt.fill_data[4], nxt.fill_data[5], nxt.fill_data[6], nxt.fill_data[7]);
                    // AXI read-path check: returned cacheline should match backing memory.
                    if (p_memory != nullptr)
                    {
                        const uint32_t line_addr = fill_line_addr;
                        const uint32_t word_base = (line_addr >> 2);
                        bool read_mismatch = false;
                        int first_bad = -1;
                        uint32_t axi_word = 0;
                        uint32_t mem_word = 0;
                        for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                        {
                            uint32_t exp = p_memory[word_base + w];
                            uint32_t got = in.axi_in.resp_data[w];
                            if (got != exp)
                            {
                                read_mismatch = true;
                                first_bad = w;
                                axi_word = got;
                                mem_word = exp;
                                break;
                            }
                        }
                        if (read_mismatch)
                        {
                            DBG_PRINTF("[AXI READ MISMATCH] cyc=%lld resp_id=%u line=0x%08x word=%d axi=0x%08x mem=0x%08x\n",
                                   (long long)sim_time, (unsigned)resp_id, line_addr,
                                   first_bad, axi_word, mem_word);
                            Assert(false && "MSHR AXI read mismatch: backing memory does not match the data returned on the AXI read channel. This likely indicates a bug in the MSHR logic, the AXI interface handling, or the memory model.");
                        }
                    }
                    mshr_entries_nxt[resp_id].valid = false; // auto-consume the fill response
                    mshr_entries_nxt[resp_id].issued = false;
                    mshr_entries_nxt[resp_id].fill = false;
                    nxt.fill = true;
                    nxt.fill_addr = fill_line_addr;
                    if (nxt.mshr_count > 0)
                    {
                        nxt.mshr_count--;
                    }
                }
            }
            else
            {
            }
        }
        else
        {
            Assert(false && "Invalid MSHR response ID");
        }
    }

    // ── Issue next pending AR ─────────────────────────────────────────────────
    // Handshake note:
    // `in.axi_in.req_ready` is sampled before this module drives current-cycle
    // `req_valid`, so treat it as acknowledgement for a previously held request.
    // Keep one pending AR asserted until we observe ready in a later cycle.
    for(int i=0; i<MSHR_ENTRIES; i++){
        const MSHREntry &ce = mshr_entries[i];
        if (!ce.valid || ce.issued)
            continue;
        if (in.axi_in.req_ready){
            mshr_entries_nxt[i].issued = true;
            axi_issue_cycle[i] = static_cast<uint64_t>(sim_time);
            axi_issue_cycle_valid[i] = true;
        }
        else{

        }
        break;
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// seq — advance state on the clock edge.
//
// 1. Auto-consume the fill that was delivered this cycle (out.fill_idx).
// 2. cur = nxt.
// 3. Retire or promote fill_consumed entries.
// 4. nxt = cur (reset nxt for next-cycle mutations).
// ─────────────────────────────────────────────────────────────────────────────
void MSHR::seq()
{

    cur = nxt;

    memcpy(mshr_entries, mshr_entries_nxt, sizeof(mshr_entries));

    nxt = cur;
}
