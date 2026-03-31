#include "RealDcache.h"
#include "PhysMemory.h"
#include <oracle.h>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char *kColorReset     = "\033[0m";
constexpr const char *kColorLoadReq   = "\033[1;36m"; // Cyan
constexpr const char *kColorStoreReq  = "\033[1;33m"; // Yellow
constexpr const char *kColorLoadResp  = "\033[1;32m"; // Green
constexpr const char *kColorStoreResp = "\033[1;35m"; // Magenta

struct PendingMissLine {
    bool valid = false;
    uint32_t set_idx = 0;
    uint32_t tag = 0;
};

bool pending_miss_contains(const PendingMissLine *pending_miss_lines,
                           int pending_miss_count, uint32_t set_idx,
                           uint32_t tag) {
    for (int idx = 0; idx < pending_miss_count; idx++) {
        if (!pending_miss_lines[idx].valid) {
            continue;
        }
        if (pending_miss_lines[idx].set_idx == set_idx &&
            pending_miss_lines[idx].tag == tag) {
            return true;
        }
    }
    return false;
}

void pending_miss_add(PendingMissLine *pending_miss_lines,
                      int &pending_miss_count, int pending_miss_capacity,
                      uint32_t set_idx, uint32_t tag) {
    if (pending_miss_contains(pending_miss_lines, pending_miss_count, set_idx,
                              tag)) {
        return;
    }
    Assert(pending_miss_count < pending_miss_capacity);
    pending_miss_lines[pending_miss_count].valid = true;
    pending_miss_lines[pending_miss_count].set_idx = set_idx;
    pending_miss_lines[pending_miss_count].tag = tag;
    pending_miss_count++;
}

}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / init
// ─────────────────────────────────────────────────────────────────────────────
// RealDcache::RealDcache(axi_interconnect::AXI_Interconnect *ic)
//     : ic_(ic) {}

void RealDcache::init() {
    init_dcache();
    s1s2_cur = {};
    s1s2_nxt = {};
    for (auto &pending_write : pending_writes_) {
        pending_write = {};
    }
    for (auto &lru_update : lru_updates_) {
        lru_update = {};
    }
    for (auto &req_track : req_track_) {
        req_track = {};
    }
    req_track_rr_ = 0;

}

bool RealDcache::begin_req_track(bool is_store, size_t req_id, uint32_t rob_idx,
                                 uint32_t rob_flag) {
    int free_idx = -1;
    for (int i = 0; i < kReqTrackSize; i++) {
        const auto &e = req_track_[i];
        if (!e.valid) {
            if (free_idx < 0) {
                free_idx = i;
            }
            continue;
        }
        if (e.is_store == is_store && e.req_id == req_id && e.rob_idx == rob_idx &&
            e.rob_flag == rob_flag) {
            return true;
        }
    }
    int use_idx = free_idx;
    if (use_idx < 0) {
        use_idx = req_track_rr_;
        req_track_rr_ = (req_track_rr_ + 1) % kReqTrackSize;
    }
    req_track_[use_idx].valid = true;
    req_track_[use_idx].is_store = is_store;
    req_track_[use_idx].req_id = req_id;
    req_track_[use_idx].rob_idx = rob_idx;
    req_track_[use_idx].rob_flag = rob_flag;
    req_track_[use_idx].first_cycle =
        (sim_time >= 0) ? static_cast<uint64_t>(sim_time) : 0;
    return false;
}

void RealDcache::end_req_track(bool is_store, size_t req_id, uint32_t rob_idx,
                               uint32_t rob_flag) {
    for (int i = 0; i < kReqTrackSize; i++) {
        auto &e = req_track_[i];
        if (!e.valid) {
            continue;
        }
        if (e.is_store == is_store && e.req_id == req_id && e.rob_idx == rob_idx &&
            e.rob_flag == rob_flag) {
            if (ctx != nullptr && sim_time >= 0) {
                const uint64_t now = static_cast<uint64_t>(sim_time);
                if (now >= e.first_cycle) {
                    ctx->perf.l1d_mem_inst_total_cycles += (now - e.first_cycle);
                    ctx->perf.l1d_mem_inst_samples++;
                }
            }
            e = {};
            return;
        }
    }
}

bool RealDcache::special_load_addr(uint32_t addr,uint32_t &mem_val,MicroOp &uop){
    // Timer addresses (0x1fd0e000, 0x1fd0e004) are classified as MMIO and
    // should be routed through PeripheralAxi instead of DCache.
    Assert(addr != OPENSBI_TIMER_LOW_ADDR && addr != OPENSBI_TIMER_HIGH_ADDR &&
           "Timer address reached DCache! Should be routed via MMIO path.");
    (void)mem_val;
    uop.dbg.difftest_skip = false;
    return false;
}

RealDcache::CoherentQueryResult
RealDcache::query_coherent_word(uint32_t addr, uint32_t &data) const {
    const AddrFields f = decode(addr);

    for (int w = 0; w < DCACHE_WAYS; w++) {
        if (!valid_array[f.set_idx][w] || tag_array[f.set_idx][w] != f.tag) {
            continue;
        }
        data = data_array[f.set_idx][w][f.word_off];
        for (int p = 0; p < LSU_STA_COUNT; p++) {
            const auto &pw = pending_writes_[p];
            if (!pw.valid || pw.set_idx != f.set_idx ||
                pw.way_idx != static_cast<uint32_t>(w) ||
                pw.word_off != f.word_off) {
                continue;
            }
            apply_strobe(data, pw.data, pw.strb);
        }
        return CoherentQueryResult::Hit;
    }

    for (int i = 0; i < DCACHE_WB_ENTRIES; i++) {
        if (!write_buffer[i].valid ||
            !cache_line_match(write_buffer[i].addr, addr)) {
            continue;
        }
        data = write_buffer[i].data[f.word_off];
        return CoherentQueryResult::Hit;
    }

    if (find_mshr_entry(f.set_idx, f.tag)) {
        return CoherentQueryResult::Retry;
    }

    return CoherentQueryResult::Miss;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 1 — called from comb().
//
// Reads incoming requests from lsu2dcache, detects bank conflicts, and
// snapshots the SRAM arrays into s1s2_nxt.
// ─────────────────────────────────────────────────────────────────────────────
void RealDcache::stage1_comb() {
    s1s2_nxt = {};

    bool mshr_fill = mshr2dcache->fill.valid;
    AddrFields mshr_f = decode(mshr2dcache->fill.addr);
    uint32_t mshr_fill_bank = mshr_fill ? mshr_f.bank : ~0u;         

    struct ReqInfo {
        bool     valid;
        AddrFields f;
        bool     conflict;
        bool     mshr_hit;
    } reqs[LSU_LDU_COUNT + LSU_STA_COUNT] = {};

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const LoadReq &req = lsu2dcache->req_ports.load_ports[i];
        reqs[i].valid = req.valid;
        if (req.valid)
            reqs[i].f.bank = decode(req.addr).bank;
        reqs[i].f =  decode(req.addr);
    }
    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const StoreReq &req = lsu2dcache->req_ports.store_ports[i];
        int idx = LSU_LDU_COUNT + i;
        reqs[idx].valid = req.valid;
        reqs[idx].f = decode(req.addr);
    }

    // ① Inter-request conflicts: each request loses to the first earlier
    //   request on the same bank (priority: lower index wins).
    // for (int i = 1; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
    //     if (!reqs[i].valid) continue;
    //     for (int j = 0; j < i; j++) {
    //         if (reqs[j].valid && reqs[j].f.bank == reqs[i].f.bank) {
    //             reqs[i].conflict = true;
    //             break;
    //         }
    //     }
    // }


    // ② Fill bank conflict: the MSHR fill write occupies one SRAM bank this
    //   cycle.  Any request (including port 0) to the same bank must be
    //   replayed so it reads the correct post-fill SRAM state next cycle.
    for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
        if (!reqs[i].valid || reqs[i].conflict) continue;
        if (mshr_fill && reqs[i].f.bank == mshr_fill_bank)
            reqs[i].conflict = true;
    }//可优化：MSHR replay

    for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
        if (!reqs[i].valid || reqs[i].conflict) continue;
        // Only snapshot true MSHR ownership here.
        // Same-cycle miss allocations are tracked later in stage2 after the
        // older request is proven to be a real miss rather than a hit/bypass.
        reqs[i].mshr_hit = find_mshr_entry(reqs[i].f.set_idx, reqs[i].f.tag);
    }

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const LoadReq &req = lsu2dcache->req_ports.load_ports[i];
        S1S2Reg::LoadSlot &slot = s1s2_nxt.loads[i];
        if (!req.valid) continue;

        slot.valid    = true;
        slot.addr     = req.addr;
        slot.uop      = req.uop;
        slot.req_id   = req.req_id;
        slot.replayed = reqs[i].conflict;
        slot.mshr_hit = reqs[i].mshr_hit;
        AddrFields f  = decode(req.addr);
        slot.set_idx  = f.set_idx;
        if(reqs[i].conflict) continue; // Don't need to snapshot SRAM if we're going to replay due to bank conflict
        for (int w = 0; w < DCACHE_WAYS; w++) {
            slot.tag_snap[w]   = tag_array[f.set_idx][w];
            slot.valid_snap[w] = valid_array[f.set_idx][w];
            slot.dirty_snap[w] = dirty_array[f.set_idx][w];
            for (int d = 0; d < DCACHE_LINE_WORDS; d++)
                slot.data_snap[w][d] = data_array[f.set_idx][w][d];
        }
    }

    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const StoreReq &req = lsu2dcache->req_ports.store_ports[i];
        S1S2Reg::StoreSlot &slot = s1s2_nxt.stores[i];
        if (!req.valid) continue;

        int idx       = LSU_LDU_COUNT + i;
        slot.valid    = true;
        slot.addr     = req.addr;
        slot.data     = req.data;
        slot.strb     = static_cast<uint8_t>(req.strb);
        slot.uop      = req.uop;
        slot.req_id   = req.req_id;
        slot.replayed = reqs[idx].conflict;
        slot.mshr_hit = reqs[idx].mshr_hit;
        AddrFields f  = decode(req.addr);
        slot.set_idx  = f.set_idx;
        if(reqs[idx].conflict) continue; // Don't need to snapshot SRAM if we're going to replay due to bank conflict
        for (int w = 0; w < DCACHE_WAYS; w++) {
            slot.tag_snap[w]   = tag_array[f.set_idx][w];
            slot.valid_snap[w] = valid_array[f.set_idx][w];
            slot.dirty_snap[w] = dirty_array[f.set_idx][w];
            for (int d = 0; d < DCACHE_LINE_WORDS; d++)
                slot.data_snap[w][d] = data_array[f.set_idx][w][d];
        }
    }
}

void RealDcache::prepare_wb_queries_for_stage2() {
    Assert(dcache2wb != nullptr && "dcache2wb pointer not set");

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        dcache2wb->bypass_req[i] = {};
        const S1S2Reg::LoadSlot &slot = s1s2_cur.loads[i];
        if (!slot.valid || slot.replayed) {
            continue;
        }
        dcache2wb->bypass_req[i].valid = true;
        dcache2wb->bypass_req[i].addr = slot.addr;
    }

    for (int i = 0; i < LSU_STA_COUNT; i++) {
        dcache2wb->merge_req[i] = {};
        const S1S2Reg::StoreSlot &slot = s1s2_cur.stores[i];
        if (!slot.valid || slot.replayed) {
            continue;
        }
        dcache2wb->merge_req[i].valid = true;
        dcache2wb->merge_req[i].addr = slot.addr;
        dcache2wb->merge_req[i].data = slot.data;
        dcache2wb->merge_req[i].strb = slot.strb;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 2 — called from comb().
//
// Processes s1s2_cur (the pipeline register from the previous cycle):
//   - Tag comparison → hit or miss.
//   - Hit: extract data, form response.
//   - Miss: set mshr_.in / wb_.in signals; replay if resources are full.
//
// Reads mshr_.out (populated by comb_outputs()) and wb_.out.
// Sets mshr_.in.ports[] and wb_.in.push_ports[] signals to be processed
// by mshr_.comb_inputs() / wb_.comb_inputs() called afterwards.
// ─────────────────────────────────────────────────────────────────────────────
void RealDcache::stage2_comb() {
    
    dcache2lsu->resp_ports.clear();
    for (auto &pending_write : pending_writes_) {
        pending_write = {};
    }
    for (auto &lru_update : lru_updates_) {
        lru_update = {};
    }

    uint32_t mshr_free_entries = mshr2dcache->free;
    AddrFields mshr_f = decode(mshr2dcache->fill.addr);
    PendingMissLine pending_miss_lines[LSU_LDU_COUNT + LSU_STA_COUNT] = {};
    int pending_miss_count = 0;

    // ── Load ports ────────────────────────────────────────────────────────────
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        dcache2mshr->load_reqs[i].valid = false; // Default to no load request; set to true on load miss
        const S1S2Reg::LoadSlot &slot = s1s2_cur.loads[i];
        LoadResp &resp = dcache2lsu->resp_ports.load_resps[i];

        if (!slot.valid) continue;
        if (ctx != nullptr) {
            ctx->perf.l1d_req_all++;
        }
        const bool is_replay_req =
            begin_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
        if (ctx != nullptr) {
            if (is_replay_req) {
                ctx->perf.l1d_req_replay++;
            } else {
                ctx->perf.l1d_req_initial++;
                ctx->perf.dcache_access_num++;
            }
        }

        if (slot.replayed) {
            resp.valid  = true;
            resp.replay = 3;
            resp.req_id = slot.req_id;
            resp.uop    = slot.uop;
            if (ctx != nullptr) {
                ctx->perf.l1d_replay_bank_conflict++;
                ctx->perf.l1d_replay_bank_conflict_load++;
            }
            if (ctx != nullptr && is_replay_req) {
                ctx->perf.l1d_replay_squash_abort++;
            }
            LSU_MEM_DBG_PRINTF("%s[DCACHE LOAD RESP] cyc=%lld port=%d replay=3(bank_conflict) req_id=%zu slot.uop.rob_idx=%u addr=0x%08x reg=%d %s\n",
                   kColorLoadResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, slot.uop.dest_preg, kColorReset);
            continue;
        }

        AddrFields f          = decode(slot.addr);
        uint32_t tag_expected = f.tag;
        const bool mshr_pending_line =
            pending_miss_contains(pending_miss_lines, pending_miss_count,
                                  slot.set_idx, tag_expected) ||
            slot.mshr_hit || find_mshr_entry(slot.set_idx, tag_expected);

        uint32_t mem_val = 0;
        MicroOp response_uop = slot.uop;

        bool is_special = special_load_addr(slot.addr, mem_val, response_uop);

        int hit_way = -1;
        for (int w = 0; w < DCACHE_WAYS; w++) {
            if (slot.valid_snap[w] && slot.tag_snap[w] == tag_expected) {
                hit_way = w;
                break;
            }
        }
        const bool mshr_fill_match =
            mshr2dcache->fill.valid && mshr_f.set_idx == slot.set_idx &&
            mshr_f.tag == tag_expected;

        if(is_special){
            resp.valid = true;
            resp.data = mem_val;
            resp.uop = response_uop;
            resp.replay = 0;
            resp.req_id = slot.req_id;
            end_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
        }
        else if (mshr_pending_line && mshr_fill_match) {
            resp.valid = true;
            resp.data = mshr2dcache->fill.data[f.word_off];
            resp.uop = slot.uop;
            resp.replay = 0;
            resp.req_id = slot.req_id;
            end_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
        }
        else if (mshr_pending_line) {
            // Once a line has an active MSHR/fill in flight, the cache snapshot
            // seen in s1s2_cur can be stale relative to the eventual refill or
            // same-line store merges. Returning a plain hit here lets PTW/LSU
            // observe transient old PTE/data values and commit false faults.
            resp.valid = true;
            resp.replay = 2;
            resp.req_id = slot.req_id;
            resp.uop = slot.uop;
            if (ctx != nullptr) {
                ctx->perf.l1d_replay_wait_mshr++;
                ctx->perf.l1d_replay_wait_mshr_load++;
                ctx->perf.l1d_replay_wait_mshr_hit++;
            }
            if (ctx != nullptr && is_replay_req) {
                ctx->perf.l1d_replay_squash_abort++;
            }
            LSU_MEM_DBG_PRINTF("%s[DCACHE LOAD RESP] cyc=%lld port=%d replay=2(mshr_pending_guard) req_id=%zu slot.uop.rob_idx=%u addr=0x%08x%s\n",
                   kColorLoadResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
        }
        else if (wb2dcache->bypass_resp[i].valid) {
            // Same-line dirty victims in WriteBuffer are newer than a clean
            // line that may have been refilled into DCache before WB drains.
            resp.valid = true;
            resp.replay = 0;
            resp.data = wb2dcache->bypass_resp[i].data;
            resp.uop = slot.uop;
            resp.req_id = slot.req_id;
            end_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
        }
        else if (hit_way >= 0 ) {
            // ── Cache Hit ────────────────────────────────────────────────────
            resp.valid  = true;
            resp.replay = 0;
            resp.data   = slot.data_snap[hit_way][f.word_off];
            resp.uop    = slot.uop;
            resp.req_id = slot.req_id;
            end_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
            lru_updates_[i] = {true, slot.set_idx, hit_way};
        } else {
            if(mshr_fill_match){
                resp.valid = true;
                resp.data = mshr2dcache->fill.data[f.word_off];
                resp.uop = slot.uop;
                resp.replay = 0; // waiting for fill to complete, replay next cycle
                resp.req_id = slot.req_id;
                end_req_track(false, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
            }
            else if (mshr_pending_line) {
                resp.valid = true;
                resp.replay = 2; // MSHR full, replay later
                resp.req_id = slot.req_id;
                resp.uop = slot.uop;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_wait_mshr++;
                    ctx->perf.l1d_replay_wait_mshr_load++;
                    ctx->perf.l1d_replay_wait_mshr_hit++;
                }
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE LOAD RESP] cyc=%lld port=%d replay=2(mshr_hit) req_id=%zu slot.uop.rob_idx=%u addr=0x%08x%s\n",
                       kColorLoadResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
            }
            else if(mshr_free_entries == 0){
                    resp.valid = true;
                    resp.replay = 1; // MSHR full, replay later
                    resp.req_id = slot.req_id;
                    resp.uop = slot.uop;
                    if (ctx != nullptr) {
                        ctx->perf.l1d_replay_mshr_full++;
                        ctx->perf.l1d_replay_mshr_full_load++;
                    }
                    if (ctx != nullptr && is_replay_req) {
                        ctx->perf.l1d_replay_squash_abort++;
                    }
                    LSU_MEM_DBG_PRINTF("%s[DCACHE LOAD RESP] cyc=%lld port=%d replay=1(mshr_full) req_id=%zu slot.uop.rob_idx=%u addr=0x%08x%s\n",
                           kColorLoadResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
            }
            else{
                dcache2mshr->load_reqs[i].valid = true;
                dcache2mshr->load_reqs[i].addr  = slot.addr;
                dcache2mshr->load_reqs[i].uop   = slot.uop;
                dcache2mshr->load_reqs[i].req_id = slot.req_id;
                pending_miss_add(pending_miss_lines, pending_miss_count,
                                 LSU_LDU_COUNT + LSU_STA_COUNT, slot.set_idx,
                                 tag_expected);
                // First miss allocation must still return a replay response,
                // otherwise LSU keeps waiting forever for a one-shot request.
                resp.valid = true;
                resp.replay = 2;
                resp.req_id = slot.req_id;
                resp.uop = slot.uop;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_wait_mshr++;
                    ctx->perf.l1d_replay_wait_mshr_load++;
                    ctx->perf.l1d_replay_wait_mshr_first_alloc++;
                    ctx->perf.l1d_miss_mshr_alloc++;
                    ctx->perf.dcache_miss_num++;
                    if (is_replay_req) {
                        ctx->perf.l1d_replay_squash_abort++;
                    }
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE LOAD RESP] cyc=%lld port=%d replay=2(first_mshr_alloc) req_id=%zu slot.uop.rob_idx=%u addr=0x%08x%s\n",
                       kColorLoadResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
                mshr_free_entries = mshr_free_entries - 1;
            }
        } 
    }

    // ── Store ports ───────────────────────────────────────────────────────────
    for (int i = 0; i < LSU_STA_COUNT; i++) {
        dcache2mshr->store_reqs[i].valid = false; // Default to no store request; set to true on store miss
        dcache2mshr->store_hit_updates[i].valid = false;
        const S1S2Reg::StoreSlot &slot = s1s2_cur.stores[i];
        StoreResp &resp = dcache2lsu->resp_ports.store_resps[i];

        if (!slot.valid) continue;
        if (ctx != nullptr) {
            ctx->perf.l1d_req_all++;
        }
        const bool is_replay_req =
            begin_req_track(true, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
        if (ctx != nullptr) {
            if (is_replay_req) {
                ctx->perf.l1d_req_replay++;
            } else {
                ctx->perf.l1d_req_initial++;
                ctx->perf.dcache_access_num++;
            }
        }

        if (slot.replayed) {
            resp.valid  = true;
            resp.replay = 3;
            resp.req_id = slot.req_id;
            if (ctx != nullptr) {
                ctx->perf.l1d_replay_bank_conflict++;
                ctx->perf.l1d_replay_bank_conflict_store++;
            }
            if (ctx != nullptr && is_replay_req) {
                ctx->perf.l1d_replay_squash_abort++;
            }
            LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=3(bank_conflict) req_id=%zu rob=%u addr=0x%08x%s\n",
                   kColorStoreResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
            continue;
        }

        AddrFields f          = decode(slot.addr);
        uint32_t tag_expected = f.tag;

        int hit_way = -1;
        for (int w = 0; w < DCACHE_WAYS; w++) {
            if (slot.valid_snap[w] && slot.tag_snap[w] == tag_expected) {
                hit_way = w;
                break;
            }
        }

        if (hit_way >= 0) {
            // ── Store Hit ────────────────────────────────────────────────────

            // The line has been reallocated in DCache, but an older eviction of
            // the same line is already on the AXI write path and can no longer
            // absorb merges. Accepting the hit here would let cache state move
            // ahead of the in-flight writeback snapshot, so force a replay
            // until the older WB entry drains.
            if (wb2dcache->merge_resp[i].busy) {
                resp.valid = true;
                resp.replay = 3;
                resp.req_id = slot.req_id;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_bank_conflict++;
                    ctx->perf.l1d_replay_bank_conflict_store++;
                }
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=3(hit_wb_busy) req_id=%zu rob=%u addr=0x%08x%s\n",
                       kColorStoreResp, (long long)sim_time, i, slot.req_id,
                       slot.uop.rob_idx, slot.addr, kColorReset);
            }

            // If fill writes the same set/way in this cycle, this hit-line is
            // being replaced at seq(). A store-hit "success" here would be
            // overwritten by fill commit, so force replay.
            else if (mshr2dcache->fill.valid &&
                mshr_f.set_idx == slot.set_idx &&
                static_cast<int>(mshr2dcache->fill.way) == hit_way) {
                resp.valid = true;
                resp.replay = 3;
                resp.req_id = slot.req_id;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_wait_mshr++;
                    ctx->perf.l1d_replay_wait_mshr_store++;
                    ctx->perf.l1d_replay_wait_mshr_fill_wait++;
                }
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=3(fill_replace_conflict) req_id=%zu rob=%u addr=0x%08x fill_line=0x%08x fill_way=%u hit_way=%d%s\n",
                       kColorStoreResp, (long long)sim_time, i, slot.req_id,
                       slot.uop.rob_idx, slot.addr, mshr2dcache->fill.addr,
                       mshr2dcache->fill.way, hit_way, kColorReset);
            }else {
                resp.valid  = true;
                resp.replay = 0;
                resp.req_id = slot.req_id;
                end_req_track(true, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);

                pending_writes_[i] = {
                    true,
                    slot.set_idx,
                    static_cast<uint32_t>(hit_way),
                    f.word_off,
                    slot.data,
                    slot.strb
                };
                dcache2mshr->store_hit_updates[i].valid = true;
                dcache2mshr->store_hit_updates[i].set_idx = slot.set_idx;
                dcache2mshr->store_hit_updates[i].way_idx =
                    static_cast<uint32_t>(hit_way);
                dcache2mshr->store_hit_updates[i].word_off = f.word_off;
                dcache2mshr->store_hit_updates[i].data = slot.data;
                dcache2mshr->store_hit_updates[i].strb = slot.strb;
                lru_updates_[LSU_LDU_COUNT + i] = {true, slot.set_idx, hit_way};
            }
        } else {
            if(wb2dcache->merge_resp[i].valid){
                resp.valid = true;
                resp.replay = 0;
                resp.req_id = slot.req_id;
                end_req_track(true, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
            }
            else if (wb2dcache->merge_resp[i].busy) {
                // The matching writeback line has already been issued to AXI.
                // Do not ack the store and do not allocate a fresh MSHR for the
                // same line; simply retry after the in-flight WB drains.
                resp.valid = true;
                resp.replay = 3;
                resp.req_id = slot.req_id;
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=3(wb_busy) req_id=%zu rob=%u addr=0x%08x%s\n",
                       kColorStoreResp, (long long)sim_time, i, slot.req_id,
                       slot.uop.rob_idx, slot.addr, kColorReset);
            }
            else if(mshr2dcache->fill.valid && mshr_f.set_idx == slot.set_idx && mshr_f.tag == tag_expected){
                resp.valid = true;
                resp.replay = 2; // waiting for fill to complete, replay next cycle
                resp.req_id = slot.req_id;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_wait_mshr++;
                    ctx->perf.l1d_replay_wait_mshr_store++;
                    ctx->perf.l1d_replay_wait_mshr_fill_wait++;
                }
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=2(fill_wait) req_id=%zu rob=%u addr=0x%08x%s\n",
                       kColorStoreResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
            }
            else if (pending_miss_contains(pending_miss_lines,
                                           pending_miss_count, slot.set_idx,
                                           tag_expected) ||
                     slot.mshr_hit || find_mshr_entry(slot.set_idx, tag_expected)) {
                dcache2mshr->store_reqs[i].valid = true;
                dcache2mshr->store_reqs[i].addr  = slot.addr;
                dcache2mshr->store_reqs[i].data  = slot.data;
                dcache2mshr->store_reqs[i].strb  = slot.strb;
                dcache2mshr->store_reqs[i].uop   = slot.uop;
                dcache2mshr->store_reqs[i].req_id = slot.req_id;
                resp.valid = true;
                resp.replay = 0;
                resp.req_id = slot.req_id;
                end_req_track(true, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
            }
            else if(mshr_free_entries == 0){
                resp.valid = true;
                resp.replay = 1; // MSHR full, replay later
                resp.req_id = slot.req_id;
                if (ctx != nullptr) {
                    ctx->perf.l1d_replay_mshr_full++;
                    ctx->perf.l1d_replay_mshr_full_store++;
                }
                if (ctx != nullptr && is_replay_req) {
                    ctx->perf.l1d_replay_squash_abort++;
                }
                LSU_MEM_DBG_PRINTF("%s[DCACHE STORE RESP] cyc=%lld port=%d replay=1(mshr_full) req_id=%zu rob=%u addr=0x%08x%s\n",
                       kColorStoreResp, (long long)sim_time, i, slot.req_id, slot.uop.rob_idx, slot.addr, kColorReset);
            }
            else {
                dcache2mshr->store_reqs[i].valid = true;
                dcache2mshr->store_reqs[i].addr  = slot.addr;
                dcache2mshr->store_reqs[i].data  = slot.data;
                dcache2mshr->store_reqs[i].strb  = slot.strb;
                dcache2mshr->store_reqs[i].uop   = slot.uop;
                dcache2mshr->store_reqs[i].req_id = slot.req_id;
                pending_miss_add(pending_miss_lines, pending_miss_count,
                                 LSU_LDU_COUNT + LSU_STA_COUNT, slot.set_idx,
                                 tag_expected);
                resp.valid = true;
                resp.replay = 0;
                resp.req_id = slot.req_id;
                resp.is_cache_miss = true;
                end_req_track(true, slot.req_id, slot.uop.rob_idx, slot.uop.rob_flag);
                if (ctx != nullptr) {
                    ctx->perf.l1d_miss_mshr_alloc++;
                    ctx->perf.dcache_miss_num++;
                }
                mshr_free_entries = mshr_free_entries - 1;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// comb — top-level combinational evaluation for one cycle.
//
// Ordering:
//   1. Set mshr_.in lookups — provide s1s2_cur line addresses (stable from seq)
//   2. mshr_.comb_outputs() — compute lookup results, fill delivery, full flag
//      MUST precede stage1_comb() so fill_set_idx is known for bank-conflict
//      detection against the current cycle's new requests.
//   3. wb_.comb_outputs()   — compute full / free_count
//   4. stage1_comb()        — snapshot SRAMs, detect bank conflicts
//      (inter-request + fill-bank conflicts both handled here)
//   5. stage2_comb()        — tag compare, responses, set alloc/secondary/push
//   6. Bridge IC outputs → mshr_.axi_in, wb_.axi_in
//   7. mshr_.comb_inputs()  — process alloc/secondary, fill axi_out
//   8. wb_.comb_inputs()    — process pushes, fill axi_out
//   9. Bridge mshr_.axi_out, wb_.axi_out → IC inputs
// ─────────────────────────────────────────────────────────────────────────────
void RealDcache::comb() {
    assert(lsu2dcache  != nullptr && "lsu2dcache pointer not set");
    assert(dcache2lsu  != nullptr && "dcache2lsu pointer not set");

    stage1_comb();
    prepare_wb_queries_for_stage2();

    stage2_comb();


    // dcache2lsu->resp_ports.mshr_replay = mshr_.out.mshr_replay;
}

// ─────────────────────────────────────────────────────────────────────────────
// seq — advance all state on the simulated clock edge.
// ─────────────────────────────────────────────────────────────────────────────
void RealDcache::seq() {
    // 1. Advance pipeline register.
    s1s2_cur = s1s2_nxt;

    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const PendingWrite &pw = pending_writes_[i];
        if (!pw.valid) continue;
        apply_strobe(data_array[pw.set_idx][pw.way_idx][pw.word_off],
                     pw.data, pw.strb);
        dirty_array[pw.set_idx][pw.way_idx] = true;
    }


    // 2. Apply store hits.
    if (mshr2dcache->fill.valid) {
        write_dcache_line(decode(mshr2dcache->fill.addr).set_idx,
                          mshr2dcache->fill.way,
                          decode(mshr2dcache->fill.addr).tag,
                          mshr2dcache->fill.data);
        if (mshr2dcache->fill.dirty) {
            dirty_array[decode(mshr2dcache->fill.addr).set_idx]
                       [mshr2dcache->fill.way] = true;
        }
    }

    // 4. Apply LRU updates from hit accesses this cycle.
    for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
        const LruUpdate &u = lru_updates_[i];
        if (!u.valid || u.way < 0) continue;
        lru_reset(u.set_idx, static_cast<uint32_t>(u.way));
    }

}
