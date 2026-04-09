#include "MSHR.h"
#include "DeadlockReplayTrace.h"
#include "PhysMemory.h"

#include <cassert>
#include <cstdio>
#include <cstring>

MSHREntry mshr_entries_nxt[DCACHE_MSHR_ENTRIES];

namespace {
static constexpr uint8_t kCacheLineReqTotalSize =
    static_cast<uint8_t>(DCACHE_LINE_BYTES - 1u);

#ifndef CONFIG_DEBUG_FOCUS_DCACHE_LINE0
#define CONFIG_DEBUG_FOCUS_DCACHE_LINE0 0u
#endif

#ifndef CONFIG_DEBUG_FOCUS_DCACHE_LINE1
#define CONFIG_DEBUG_FOCUS_DCACHE_LINE1 0u
#endif

inline uint32_t cache_line_base(uint32_t addr) {
    return addr & ~static_cast<uint32_t>(DCACHE_LINE_BYTES - 1u);
}

bool focus_dcache_line(uint32_t addr) {
    const uint32_t line = cache_line_base(addr);
    return (CONFIG_DEBUG_FOCUS_DCACHE_LINE0 != 0u &&
            line == static_cast<uint32_t>(CONFIG_DEBUG_FOCUS_DCACHE_LINE0)) ||
           (CONFIG_DEBUG_FOCUS_DCACHE_LINE1 != 0u &&
            line == static_cast<uint32_t>(CONFIG_DEBUG_FOCUS_DCACHE_LINE1));
}

void log_focus_mshr_words(const char *tag, uint32_t addr, int slot,
                          const uint32_t *words) {
    if (!focus_dcache_line(addr)) {
        return;
    }
    std::printf(
        "[FOCUS][MSHR][%s] cyc=%lld slot=%d line=0x%08x data=[%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x]\n",
        tag, (long long)sim_time, slot, cache_line_base(addr), words[0],
        words[1], words[2], words[3], words[4], words[5], words[6], words[7],
        words[8], words[9], words[10], words[11], words[12], words[13],
        words[14], words[15]);
}

bool victim_has_same_cycle_store_hit(const DcacheMSHRIO &dcachemshr,
                                     uint32_t set_idx, uint32_t way_idx) {
    for (int p = 0; p < LSU_STA_COUNT; ++p) {
        const auto &u = dcachemshr.store_hit_updates[p];
        if (!u.valid) {
            continue;
        }
        if (u.set_idx != set_idx || u.way_idx != way_idx) {
            continue;
        }
        return true;
    }
    return false;
}

int find_next_entry_idx(uint32_t set_idx, uint32_t tag) {
    for (int i = 0; i < DCACHE_MSHR_ENTRIES; i++) {
        const MSHREntry &entry = mshr_entries_nxt[i];
        if (entry.valid && entry.index == set_idx && entry.tag == tag) {
            return i;
        }
    }
    return -1;
}

void merge_store_into_entry(MSHREntry &entry, const StoreReq &req) {
    const AddrFields f = decode(req.addr);
    Assert(f.word_off < DCACHE_LINE_WORDS && "Store merge word offset overflow");
    const uint32_t old_data = entry.merged_store_data[f.word_off];
    apply_strobe(entry.merged_store_data[f.word_off], req.data, req.strb);
    entry.merged_store_strb[f.word_off] |= req.strb;
    entry.merged_store_dirty = true;
    if (focus_dcache_line(req.addr)) {
        std::printf(
            "[FOCUS][MSHR][STORE-MERGE] cyc=%lld line=0x%08x word=%u old=0x%08x data=0x%08x new=0x%08x strb=0x%x\n",
            (long long)sim_time, cache_line_base(req.addr),
            static_cast<unsigned>(f.word_off), old_data, req.data,
            entry.merged_store_data[f.word_off],
            static_cast<unsigned>(req.strb));
    }
}

void log_unexpected_resp(uint8_t resp_id, const char *reason, uint32_t mshr_count) {
    if (resp_id >= DCACHE_MSHR_ENTRIES) {
        LSU_MEM_DBG_PRINTF(
            "[MSHR RESP UNEXPECTED] cyc=%lld resp_id=%u reason=%s count=%u\n",
            (long long)sim_time, static_cast<unsigned>(resp_id), reason,
            mshr_count);
        return;
    }
    const MSHREntry &e = mshr_entries[resp_id];
    const uint32_t line_addr = e.valid ? get_addr(e.index, e.tag, 0) : 0u;
    LSU_MEM_DBG_PRINTF(
        "[MSHR RESP UNEXPECTED] cyc=%lld resp_id=%u reason=%s valid=%d issued=%d fill=%d line=0x%08x count=%u\n",
        (long long)sim_time, static_cast<unsigned>(resp_id), reason,
        static_cast<int>(e.valid), static_cast<int>(e.issued),
        static_cast<int>(e.fill), line_addr, mshr_count);
}
}

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

    out = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// comb_outputs — Phase 1: compute lookups, full, and fill delivery from cur.
//
// Must be called BEFORE stage2_comb() so that lookup results and full flag
// are stable when stage2_comb() decides whether to alloc or add_secondary.
// ─────────────────────────────────────────────────────────────────────────────
void MSHR::comb_outputs()
{   
    out.mshr2dcache.free = DCACHE_MSHR_ENTRIES - cur.mshr_count;
    out.replay_resp.replay = cur.fill; // MSHR full replay code
    out.replay_resp.replay_addr = cur.fill_addr;
    out.replay_resp.free_slots = out.mshr2dcache.free;
    // Registered fill output (from previous cycle comb_inputs()).
    out.mshr2dcache.fill.valid = cur.fill_valid;
    out.mshr2dcache.fill.dirty = cur.fill_dirty;
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
    // Default to ready. The final next-cycle credit is refined again in
    // comb_inputs() after we know whether a live response was captured into the
    // local hold slot or whether a held response was successfully consumed.
    out.axi_out.resp_ready = true;

    for(int i=0; i<DCACHE_MSHR_ENTRIES; i++){
        const MSHREntry &ce = mshr_entries[i];
        if (!ce.valid || ce.issued)
            continue;

        out.axi_out.req_valid = true;
        out.axi_out.req_addr = get_addr(ce.index, ce.tag, 0);
        out.axi_out.req_total_size = kCacheLineReqTotalSize;
        out.axi_out.req_id = static_cast<uint8_t>(i);
        break;
    }


}

int MSHR::entries_add(int set_idx, int tag)
{   
    if(nxt.mshr_count >= DCACHE_MSHR_ENTRIES){
        Assert(0 && "MSHR full");
    }
    int alloc_idx = -1;
    for (int off = 0; off < DCACHE_MSHR_ENTRIES; off++)
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
    mshr_entries_nxt[alloc_idx].merged_store_dirty = false;
    std::memset(mshr_entries_nxt[alloc_idx].merged_store_data, 0,
                sizeof(mshr_entries_nxt[alloc_idx].merged_store_data));
    std::memset(mshr_entries_nxt[alloc_idx].merged_store_strb, 0,
                sizeof(mshr_entries_nxt[alloc_idx].merged_store_strb));
    nxt.mshr_count++;
    miss_alloc_cycle[alloc_idx] = static_cast<uint64_t>(sim_time);
    miss_alloc_cycle_valid[alloc_idx] = true;
    axi_issue_cycle[alloc_idx] = 0;
    axi_issue_cycle_valid[alloc_idx] = false;
    const uint32_t line_addr = get_addr(set_idx, tag, 0);
    if (focus_dcache_line(line_addr)) {
        std::printf(
            "[FOCUS][MSHR][ALLOC] cyc=%lld slot=%d line=0x%08x set=%d tag=0x%08x\n",
            (long long)sim_time, alloc_idx, line_addr, set_idx, tag);
    }
    return alloc_idx;
}

void MSHR::comb_inputs()
{
    // One-cycle pulse/state outputs are generated into nxt and exposed by
    // comb_outputs() in the next cycle.
    nxt.fill = false;
    nxt.fill_valid = false;
    nxt.fill_dirty = false;
    nxt.fill_way = 0;
    std::memset(nxt.fill_data, 0, sizeof(nxt.fill_data));
    nxt.wb_valid = false;
    nxt.wb_addr = 0;
    std::memset(nxt.wb_data, 0, sizeof(nxt.wb_data));

    // This ready is sampled by the interconnect in the next cycle. If we
    // capture a live response into the local hold slot this cycle, keep ready
    // asserted so the interconnect can retire the duplicated live copy on the
    // following cycle. Once hold is still occupied in a later cycle and we
    // still cannot consume it, deassert ready to block subsequent responses.
    out.axi_out.resp_ready = !cur.axi_resp_hold_valid;

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
        const AddrFields f = decode(req.addr);
        int entry_idx = find_next_entry_idx(f.set_idx, f.tag);
        if (entry_idx < 0) {
            entry_idx = entries_add(f.set_idx, f.tag);
        }
        merge_store_into_entry(mshr_entries_nxt[entry_idx], req);
    }

    // ── Accept R channel response ─────────────────────────────────────────────
    if (cur.axi_resp_hold_valid || in.axi_in.resp_valid)
    {
        const bool using_held_resp = cur.axi_resp_hold_valid;
        uint8_t resp_id =
            using_held_resp ? cur.axi_resp_hold_id : in.axi_in.resp_id;
        const uint32_t *resp_data =
            using_held_resp ? cur.axi_resp_hold_data : in.axi_in.resp_data;
        if (resp_id < DCACHE_MSHR_ENTRIES)
        {
            const MSHREntry &e_cur = mshr_entries[resp_id];
            if (e_cur.valid && e_cur.issued && !e_cur.fill)
            {
                const uint32_t fill_set = mshr_entries[resp_id].index;
                const uint32_t fill_tag = mshr_entries[resp_id].tag;
                const uint32_t lru_idx = choose_lru_victim(fill_set);
                const uint32_t victim_tag = tag_array[fill_set][lru_idx];
                const bool same_cycle_store_hit =
                    victim_has_same_cycle_store_hit(in.dcachemshr, fill_set,
                                                    lru_idx);
                bool need_wb_evict =
                    dirty_array[fill_set][lru_idx] || same_cycle_store_hit;
                bool can_consume_resp = (!need_wb_evict) || in.wbmshr.ready;
                if (!can_consume_resp)
                {
                    if (!cur.axi_resp_hold_valid)
                    {
                        nxt.axi_resp_hold_valid = true;
                        nxt.axi_resp_hold_id = resp_id;
                        std::memcpy(nxt.axi_resp_hold_data, resp_data,
                                    sizeof(nxt.axi_resp_hold_data));
                        // The live copy is now safely buffered locally. Keep
                        // next-cycle ready asserted once so the interconnect
                        // can retire that live copy instead of replaying it
                        // after hold is eventually released.
                        out.axi_out.resp_ready = true;
                    }
                    else
                    {
                        out.axi_out.resp_ready = false;
                    }
                }
                else
                {
                    nxt.axi_resp_hold_valid = false;
                    out.axi_out.resp_ready = true;
                    const uint32_t fill_line_addr = get_addr(fill_set, fill_tag, 0);
                    if (need_wb_evict)
                    {
                        nxt.wb_valid = true;
                        nxt.wb_addr = get_addr(fill_set, victim_tag, 0);
                        for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                        {
                            nxt.wb_data[w] = data_array[fill_set][lru_idx][w];
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
                            if (u.set_idx != fill_set)
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
                    // Same-cycle store miss merges were already applied into
                    // mshr_entries_nxt above. Use that snapshot so the refill
                    // line delivered to DCache includes those merged bytes
                    // instead of overwriting them with stale AXI payload.
                    const MSHREntry &e_fill = mshr_entries_nxt[resp_id];
                    nxt.fill_valid = true;
                    nxt.fill_dirty = e_fill.merged_store_dirty;
                    nxt.fill_way = lru_idx;
                    nxt.fill_addr = fill_line_addr;
                    for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                    {
                        nxt.fill_data[w] = resp_data[w];
                        if (e_fill.merged_store_strb[w] != 0) {
                            apply_strobe(nxt.fill_data[w],
                                         e_fill.merged_store_data[w],
                                         e_fill.merged_store_strb[w]);
                        }
                    }
                    log_focus_mshr_words("FILL-DATA", fill_line_addr,
                                         static_cast<int>(resp_id),
                                         nxt.fill_data);
                    // AXI read-path check: under direct-memory mode, returned
                    // cachelines must match the backing memory. Under LLC
                    // write-back mode, the response may legally come from a
                    // dirty LLC resident line that has not been written back to
                    // p_memory yet, so this comparison is no longer valid.
#if !CONFIG_AXI_LLC_ENABLE
                    if (pmem_ram_ptr() != nullptr)
                    {
                        const uint32_t line_addr = fill_line_addr;
                        bool read_mismatch = false;
                        int first_bad = -1;
                        uint32_t axi_word = 0;
                        uint32_t mem_word = 0;
                        for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                        {
                            uint32_t exp = pmem_read(line_addr + static_cast<uint32_t>(w * 4));
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
                            LSU_MEM_DBG_PRINTF("[AXI READ MISMATCH] cyc=%lld resp_id=%u line=0x%08x word=%d axi=0x%08x mem=0x%08x\n",
                                   (long long)sim_time, (unsigned)resp_id, line_addr,
                                   first_bad, axi_word, mem_word);
                            Assert(false && "MSHR AXI read mismatch: backing memory does not match the data returned on the AXI read channel. This likely indicates a bug in the MSHR logic, the AXI interface handling, or the memory model.");
                        }
                    }
#endif
                    mshr_entries_nxt[resp_id] = {};
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
                log_unexpected_resp(resp_id, "slot_not_waiting_for_fill", cur.mshr_count);
            }
        }
        else
        {
            log_unexpected_resp(resp_id, "resp_id_oob", cur.mshr_count);
            Assert(false && "Invalid MSHR response ID");
        }
    }

    // ── Issue next pending AR ─────────────────────────────────────────────────
    // Handshake note:
    // `req_ready` is only a ready-first hint. Use `req_accepted + req_accepted_id`
    // to mark exactly which MSHR slot completed AR handshake.
    if (in.axi_in.req_accepted) {
        const uint8_t acc_id = in.axi_in.req_accepted_id;
        if (acc_id < DCACHE_MSHR_ENTRIES) {
            const MSHREntry &ce = mshr_entries[acc_id];
            if (ce.valid && !ce.issued) {
                mshr_entries_nxt[acc_id].issued = true;
                axi_issue_cycle[acc_id] = static_cast<uint64_t>(sim_time);
                axi_issue_cycle_valid[acc_id] = true;
            } else {
                const uint32_t line_addr = ce.valid ? get_addr(ce.index, ce.tag, 0) : 0u;
                LSU_MEM_DBG_PRINTF(
                    "[MSHR AR ACCEPT UNEXPECTED] cyc=%lld acc_id=%u valid=%d issued=%d fill=%d line=0x%08x count=%u\n",
                    (long long)sim_time, static_cast<unsigned>(acc_id),
                    static_cast<int>(ce.valid), static_cast<int>(ce.issued),
                    static_cast<int>(ce.fill), line_addr, cur.mshr_count);
            }
        } else {
            LSU_MEM_DBG_PRINTF(
                "[MSHR AR ACCEPT UNEXPECTED] cyc=%lld acc_id=%u reason=id_oob count=%u\n",
                (long long)sim_time, static_cast<unsigned>(acc_id), cur.mshr_count);
        }
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
