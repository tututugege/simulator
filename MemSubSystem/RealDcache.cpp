#include "RealDcache.h"
#if !BSD_CONFIG
#include "types.h"
#endif
#include <cassert>
#include <cstring>
#include <MemUtils.h>

#if !BSD_CONFIG
namespace {
enum class L1DReplayReason {
    Conflict,
    BankConflict,
    MshrFull,
    WaitMshrHit,
    WaitMshrFirstAlloc,
    WaitMshrFillReq,
    WaitMshrFillWrite,
};

static bool l1d_perf_is_lsu_req(uint32_t req_id) {
    return (req_id & (1u << 31)) == 0;
}

static void count_l1d_replay(SimContext *ctx, uint32_t req_id, bool is_store,
                             bool was_replay, L1DReplayReason reason) {
    if (ctx == nullptr || !l1d_perf_is_lsu_req(req_id)) {
        return;
    }
    if (was_replay) {
        ctx->perf.l1d_replay_squash_abort++;
    }
    auto count_load_store = [&](uint64_t &total, uint64_t &load,
                                uint64_t &store) {
        total++;
        if (is_store) {
            store++;
        } else {
            load++;
        }
    };

    switch (reason) {
    case L1DReplayReason::Conflict:
        count_load_store(ctx->perf.l1d_replay_conflict,
                         ctx->perf.l1d_replay_conflict_load,
                         ctx->perf.l1d_replay_conflict_store);
        break;
    case L1DReplayReason::BankConflict:
        count_load_store(ctx->perf.l1d_replay_bank_conflict,
                         ctx->perf.l1d_replay_bank_conflict_load,
                         ctx->perf.l1d_replay_bank_conflict_store);
        break;
    case L1DReplayReason::MshrFull:
        count_load_store(ctx->perf.l1d_replay_mshr_full,
                         ctx->perf.l1d_replay_mshr_full_load,
                         ctx->perf.l1d_replay_mshr_full_store);
        break;
    case L1DReplayReason::WaitMshrHit:
        count_load_store(ctx->perf.l1d_replay_wait_mshr,
                         ctx->perf.l1d_replay_wait_mshr_load,
                         ctx->perf.l1d_replay_wait_mshr_store);
        ctx->perf.l1d_replay_wait_mshr_hit++;
        break;
    case L1DReplayReason::WaitMshrFirstAlloc:
        count_load_store(ctx->perf.l1d_replay_wait_mshr,
                         ctx->perf.l1d_replay_wait_mshr_load,
                         ctx->perf.l1d_replay_wait_mshr_store);
        break;
    case L1DReplayReason::WaitMshrFillReq:
        count_load_store(ctx->perf.l1d_replay_wait_mshr,
                         ctx->perf.l1d_replay_wait_mshr_load,
                         ctx->perf.l1d_replay_wait_mshr_store);
        ctx->perf.l1d_replay_wait_mshr_fill_wait++;
        ctx->perf.l1d_replay_wait_mshr_fill_req++;
        break;
    case L1DReplayReason::WaitMshrFillWrite:
        count_load_store(ctx->perf.l1d_replay_wait_mshr,
                         ctx->perf.l1d_replay_wait_mshr_load,
                         ctx->perf.l1d_replay_wait_mshr_store);
        ctx->perf.l1d_replay_wait_mshr_fill_wait++;
        ctx->perf.l1d_replay_wait_mshr_fill_write++;
        break;
    }
}

static void count_l1d_same_line_merge(SimContext *ctx, uint64_t count) {
    if (ctx != nullptr) {
        ctx->perf.l1d_same_line_merge += count;
    }
}

static void count_l1d_fillout_bank_grant(SimContext *ctx) {
    if (ctx != nullptr) {
        ctx->perf.l1d_fillout_bank_grant++;
    }
}

static void count_l1d_fillout_bank_conflict(SimContext *ctx, uint64_t count) {
    if (ctx != nullptr) {
        ctx->perf.l1d_fillout_bank_conflict += count;
    }
}
} // namespace
#define COUNT_L1D_REPLAY(...) count_l1d_replay(__VA_ARGS__)
#define COUNT_L1D_SAME_LINE_MERGE(...) count_l1d_same_line_merge(__VA_ARGS__)
#define COUNT_L1D_FILLOUT_BANK_GRANT(...) count_l1d_fillout_bank_grant(__VA_ARGS__)
#define COUNT_L1D_FILLOUT_BANK_CONFLICT(...) count_l1d_fillout_bank_conflict(__VA_ARGS__)
#else
#define COUNT_L1D_REPLAY(...) do {} while (0)
#define COUNT_L1D_SAME_LINE_MERGE(...) do {} while (0)
#define COUNT_L1D_FILLOUT_BANK_GRANT(...) do {} while (0)
#define COUNT_L1D_FILLOUT_BANK_CONFLICT(...) do {} while (0)
#endif

void RealDcache::init() {
    init_dcache();
    s1s2_cur = {};
    s1s2_nxt = {};
    memset(fill_req_merge_wires, 0, sizeof(fill_req_merge_wires));
}

void RealDcache::stage1_comb() {

    auto line_id = [](uint32_t addr) -> uint32_t {
        return addr >> DCACHE_OFFSET_BITS;
    };
    auto bank_index = [](const AddrFields &f) -> uint32_t {
        return static_cast<uint32_t>(f.bank);
    };

    AddrFields fill_f = decode(in.mshr2dcache->fill_req.addr);
    const uint32_t fill_line_id = line_id(in.mshr2dcache->fill_req.addr);
    
    bool same_fill_already_in_s2 =
    s1s2_cur.fill_write.valid &&
    in.mshr2dcache->fill_req.valid &&
    s1s2_cur.fill_write.set_idx == fill_f.set_idx &&
    s1s2_cur.fill_write.tag == fill_f.tag;

    const bool hold_fill_in_s2 =
        same_fill_already_in_s2 && !out.fill_write->valid;
    const bool fillout_needs_snapshot =
        in.mshr2dcache->fill_req.valid &&
        !(same_fill_already_in_s2 && out.fill_write->valid);

    bool bank_valid[DCACHE_BANK_NUM] = {};
    uint32_t bank_line_id[DCACHE_BANK_NUM] = {};
    bool bank_from_fillout[DCACHE_BANK_NUM] = {};
    bool fillout_granted = false;
    uint64_t same_line_merge_count = 0;
    uint64_t fillout_bank_conflict_count = 0;

    if (fillout_needs_snapshot) {
        const uint32_t bank = bank_index(fill_f);
        bank_valid[bank] = true;
        bank_line_id[bank] = fill_line_id;
        bank_from_fillout[bank] = true;
        fillout_granted = true;
        COUNT_L1D_FILLOUT_BANK_GRANT(ctx);
    }

    auto try_grant_bank = [&](const AddrFields &f, uint32_t req_line_id) -> bool {
        const uint32_t bank = bank_index(f);
        if (!bank_valid[bank]) {
            bank_valid[bank] = true;
            bank_line_id[bank] = req_line_id;
            bank_from_fillout[bank] = false;
            return true;
        }
        if (bank_line_id[bank] == req_line_id) {
            same_line_merge_count++;
            return true;
        }
        if (bank_from_fillout[bank]) {
            fillout_bank_conflict_count++;
        }
        return false;
    };

    if (hold_fill_in_s2) {
        s1s2_nxt.fill_write = s1s2_cur.fill_write;
    } else {
        s1s2_nxt.fill_write = {};
        if (fillout_granted && !same_fill_already_in_s2) {
            s1s2_nxt.fill_write.valid = true;
            s1s2_nxt.fill_write.id = in.mshr2dcache->fill_req.id;
            s1s2_nxt.fill_write.dirty = in.mshr2dcache->fill_req.dirty;
#if !BSD_CONFIG
            s1s2_nxt.fill_write.lsu_origin = in.mshr2dcache->fill_req.lsu_origin;
#endif
            s1s2_nxt.fill_write.set_idx = fill_f.set_idx;
            s1s2_nxt.fill_write.tag = fill_f.tag;
            memcpy(s1s2_nxt.fill_write.data,
                in.mshr2dcache->fill_req.data,
                sizeof(s1s2_nxt.fill_write.data));
        }
    }

    if (s1s2_nxt.fill_write.valid) {
        for (int i = 0; i < LSU_STA_COUNT; i++) {
            if (!fill_req_merge_wires[i].valid) continue;
            apply_strobe(s1s2_nxt.fill_write.data[fill_req_merge_wires[i].word_off],
                         fill_req_merge_wires[i].data,
                         fill_req_merge_wires[i].strb);
            s1s2_nxt.fill_write.dirty = true;
        }
    }


    memset(out.dcache2wb->bypass_req, 0, sizeof(out.dcache2wb->bypass_req));
    memset(out.dcache2wb->merge_req, 0, sizeof(out.dcache2wb->merge_req));
    memset(out.dcache2mshr->find_req, 0, sizeof(out.dcache2mshr->find_req));
    memset(out.fillout, 0, sizeof(*out.fillout));

    for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
        *out.dcachereadreq[i] = {};
    }


    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const LoadReq &req = in.lsu2dcache->req_ports.load_ports[i];
        S1S2Reg::LoadSlot &slot = s1s2_nxt.loads[i];
        if (!req.valid){
            slot.valid = false;
            slot.replayed = false;
            slot.bank_conflict = false;
#if !BSD_CONFIG
            slot.perf_replay = false;
#endif
            out.dcache2wb->bypass_req[i].valid = false;
        }
        else{
            AddrFields f  = decode(req.addr);
            const bool bank_granted = try_grant_bank(f, line_id(req.addr));
            // for(int j=0;j<LSU_STA_COUNT;j++){
            //     if(s1s2_cur.loads[j].valid&&CheckAddr(req.addr,(uint8_t)0xffff, s1s2_cur.stores[j].addr,s1s2_cur.stores[j].strb)){
            //         replay = true; // 如果有older load地址重叠且还在等待dcache响应或者等待重放中（即需要重放），则这个load也需要重放，以避免饥饿。注意我们只检查older load的状态，因为如果older load是hit，说明它可以在同一周期完成并更新cache，从而这个load不需要重放。另一方面，如果我们检查这个load自己的状态，可能会导致当有多条连续miss时出现不必要的重放，因为这个load可能还没有被标记为replay，但它会在同一周期内因为MSHR miss而被重放。
            //         break;
            //     }
            // }
            // for(int j=0;j<LSU_STA_COUNT;j++){
            //     if(in.lsu2dcache->req_ports.store_ports[j].valid&&CheckAddr(req.addr,(uint8_t)0xffff, in.lsu2dcache->req_ports.store_ports[j].addr,in.lsu2dcache->req_ports.store_ports[j].strb)){
            //         replay = true; // 如果有older store地址重叠且还在等待dcache响应或者等待重放中（即需要重放），则这个load也需要重放，以避免饥饿。注意我们只检查older store的状态，因为如果older store是hit，说明它可以在同一周期完成并更新cache，从而这个load不需要重放。另一方面，如果我们检查这个load自己的状态，可能会导致当有多条连续miss时出现不必要的重放，因为这个load可能还没有被标记为replay，但它会在同一周期内因为MSHR miss而被重放。
            //         break;
            //     }
            // }
            slot.valid    = true;
            slot.addr     = req.addr;
            slot.req_id   = req.req_id;
            slot.replayed  = !bank_granted;
            slot.bank_conflict = !bank_granted;
#if !BSD_CONFIG
            slot.perf_replay = req.replay;
#endif
            if (!bank_granted) {
                continue;
            }

            out.dcachereadreq[i]->set_idx = f.set_idx;
            out.dcache2wb->bypass_req[i].valid = true;
            out.dcache2wb->bypass_req[i].addr = req.addr;

            out.dcache2mshr->find_req[i].valid = true;
            out.dcache2mshr->find_req[i].set_idx = f.set_idx;
            out.dcache2mshr->find_req[i].tag = f.tag;
        }        
    }

    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const StoreReq &req = in.lsu2dcache->req_ports.store_ports[i];
        S1S2Reg::StoreSlot &slot = s1s2_nxt.stores[i];
        int idx       = LSU_LDU_COUNT + i;
        if(!req.valid){
            slot.valid = false;
            slot.replayed = false;
            slot.bank_conflict = false;
#if !BSD_CONFIG
            slot.perf_replay = false;
#endif
            out.dcache2wb->merge_req[i].valid = false;
        }
        else{
            AddrFields f  = decode(req.addr);
            const bool bank_granted = try_grant_bank(f, line_id(req.addr));
            // for(int j=0;j<LSU_STA_COUNT;j++){
            //     if(s1s2_cur.stores[j].valid&&CheckAddr(req.addr,req.strb, s1s2_cur.stores[j].addr,s1s2_cur.stores[j].strb)){
            //         replayed = out.dcache2lsu->resp_ports.store_resps[j].replay != ReplayType::HIT; // If there is an older store with overlapping address that is waiting for MSHR allocation or has been allocated an MSHR but not yet completed (i.e., needs to be replayed), then this store should also be replayed to avoid starvation. Note that we only check the replay status of the older store, because if the older store is a hit, it means it can complete in the same cycle and update the cache before this store's hit check, so this store doesn't need to be replayed. On the other hand, if we check the replay status of this store itself, it may cause unnecessary replays when there are multiple back-to-back misses, because this store may not have been marked as replayed yet when we check, even though it will be replayed in the same cycle due to MSHR miss.
            //         break;
            //     }
            // }
            slot.valid    = true;
            slot.addr     = req.addr;
            slot.data     = req.data;
            slot.strb     = req.strb;
            slot.req_id   = req.req_id; 
            slot.replayed = !bank_granted;
            slot.bank_conflict = !bank_granted;
#if !BSD_CONFIG
            slot.perf_replay = req.replay;
#endif
            if (!bank_granted) {
                continue;
            }

            out.dcachereadreq[idx]->set_idx = f.set_idx;
            out.dcache2wb->merge_req[i].valid = true;
            out.dcache2wb->merge_req[i].addr = req.addr;
            out.dcache2wb->merge_req[i].data = req.data;
            out.dcache2wb->merge_req[i].strb = req.strb;

            out.dcache2mshr->find_req[idx].valid = true;
            out.dcache2mshr->find_req[idx].set_idx = f.set_idx;
            out.dcache2mshr->find_req[idx].tag = f.tag;
        }
    }

    out.fillout->valid = fillout_granted;
    out.fillout->set_idx = fillout_granted ? fill_f.set_idx : 0;

    COUNT_L1D_SAME_LINE_MERGE(ctx, same_line_merge_count);
    COUNT_L1D_FILLOUT_BANK_CONFLICT(ctx, fillout_bank_conflict_count);

    s1s2_nxt.icache_req = in.lsu2dcache->icache_req;
    
}

void RealDcache::stage2_comb() {

    memset(out.dcache2lsu->resp_ports.load_resps, 0, sizeof(out.dcache2lsu->resp_ports.load_resps));
    memset(out.dcache2lsu->resp_ports.store_resps, 0, sizeof(out.dcache2lsu->resp_ports.store_resps));
    memset(out.dcache2mshr->mshr_req, 0, sizeof(out.dcache2mshr->mshr_req));
    memset(&out.dcache2wb->dirty_info, 0, sizeof(out.dcache2wb->dirty_info));
    memset(&out.dcache2mshr->fill_resp, 0, sizeof(out.dcache2mshr->fill_resp));
    
    *out.fill_write = {};
    for(int i=0;i<LSU_LDU_COUNT + LSU_STA_COUNT;i++){
        *out.lru_updates[i] = {};
        *out.pendingwrite[i] = {};
    }
    memset(fill_req_merge_wires, 0, sizeof(fill_req_merge_wires));

    if(s1s2_cur.fill_write.valid){
        int select_way = choose_plru_tree_victim(in.fillin->plru_tree_state, in.fillin->valid_snap);
        if(in.fillin->valid_snap[select_way] && in.fillin->dirty_snap[select_way]){
            if(in.wb2dcache->free>0){
                out.dcache2wb->dirty_info.valid = true;
#if !BSD_CONFIG
                out.dcache2wb->dirty_info.lsu_origin =
                    s1s2_cur.fill_write.lsu_origin;
#endif
                out.dcache2wb->dirty_info.addr = get_addr(s1s2_cur.fill_write.set_idx, in.fillin->tag_snap[select_way], 0);
                memcpy(out.dcache2wb->dirty_info.data, in.fillin->data_snap[select_way], DCACHE_WORD_NUM * sizeof(uint32_t));

                out.fill_write->valid = true;
                out.fill_write->dirty = s1s2_cur.fill_write.dirty;
#if !BSD_CONFIG
                out.fill_write->lsu_origin = s1s2_cur.fill_write.lsu_origin;
#endif
                out.fill_write->set_idx = s1s2_cur.fill_write.set_idx;
                out.fill_write->tag = s1s2_cur.fill_write.tag;
                out.fill_write->way_idx = select_way;
                memcpy(out.fill_write->data, s1s2_cur.fill_write.data, sizeof(out.fill_write->data));
                out.dcache2mshr->fill_resp.done = true; // Mark the MSHR fill as done to unblock the waiting load/store, even though the line is not yet filled into the cache. This allows the writeback to be issued in parallel with the fill, improving performance when the evicted line is dirty.
                out.dcache2mshr->fill_resp.id = s1s2_cur.fill_write.id;
            }
            else{
                out.fill_write->valid = false; // Can't accept new fill, so stall the MSHR fill response and the waiting load/store. This may cause deadlock when MSHR and WB are both full, but it's a rare case and can be resolved by replaying the load/store.
                out.dcache2wb->dirty_info.valid = false; // Can't accept new writeback, so treat the line as clean and let it be overwritten without writeback. This may cause data loss but prevents deadlock when MSHR and WB are both full.
            }
        }
        else{
            out.fill_write->valid = true;
            out.fill_write->dirty = s1s2_cur.fill_write.dirty;
#if !BSD_CONFIG
            out.fill_write->lsu_origin = s1s2_cur.fill_write.lsu_origin;
#endif
            out.fill_write->set_idx = s1s2_cur.fill_write.set_idx;
            out.fill_write->tag = s1s2_cur.fill_write.tag;
            out.fill_write->way_idx = select_way;
            memcpy(out.fill_write->data, s1s2_cur.fill_write.data, sizeof(out.fill_write->data));
            out.dcache2mshr->fill_resp.done = true; // Mark the MSHR fill as done to unblock the waiting load/store, even though the line is not yet filled into the cache. This allows the fill to complete and the line to be allocated in cache without waiting for an unnecessary writeback to drain when the evicted line is clean.
            out.dcache2mshr->fill_resp.id = s1s2_cur.fill_write.id;
        }
    }

    int mshr_free_entries = in.mshr2dcache->free;
    // ── Load ports ────────────────────────────────────────────────────────────
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const S1S2Reg::LoadSlot &slot = s1s2_cur.loads[i];
        LoadResp &resp = out.dcache2lsu->resp_ports.load_resps[i];
        DcacheLineReadResp &line_resp = *in.dcachelinereadresp[i];
        resp.valid = false; 
        if (!slot.valid) continue;

        AddrFields f          = decode(slot.addr);
        uint32_t tag_expected = f.tag;

        if(slot.replayed){
            resp.valid  = true;
            resp.replay = ReplayType::CONFLICT; // This load has been replayed due to MSHR full or conflict, so replay again to give chance for the MSHR state to be updated and avoid starvation when there are multiple back-to-back misses.
            resp.req_id = slot.req_id;
            COUNT_L1D_REPLAY(ctx, slot.req_id, false, slot.perf_replay,
                             slot.bank_conflict ? L1DReplayReason::BankConflict
                                                : L1DReplayReason::Conflict);
            continue;
        }
        int hit_way = -1;
        for (int w = 0; w < DCACHE_WAYS_NUM; w++) {
            if (line_resp.valid[w] && line_resp.tag[w] == tag_expected) {
                hit_way = w;
                break;
            }
        }

        
         if(in.wb2dcache->bypass_resp[i].valid){
            resp.valid = true;
            resp.data = in.wb2dcache->bypass_resp[i].data;
            resp.req_id = slot.req_id;
            resp.replay = ReplayType::HIT;
        }else if (hit_way >= 0 ) {
            // ── Cache Hit ────────────────────────────────────────────────────
            resp.valid  = true;
            resp.replay = ReplayType::HIT;
            resp.data   = line_resp.data[hit_way][f.word_off];
            resp.req_id = slot.req_id;
            out.lru_updates[i]->valid = true;
            out.lru_updates[i]->set_idx = f.set_idx;
            out.lru_updates[i]->way = hit_way;
        }
        else {
            if(s1s2_cur.icache_req==i){
                // ICache coherent probes are lookup-only: report miss to the
                // front-end route block without allocating an MSHR.
                resp.valid = true;
                resp.replay = ReplayType::CONFLICT;
                resp.req_id = slot.req_id;
                continue;
            }
            if(in.mshr2dcache->find_resp[i].valid){
                resp.valid = true;
                resp.replay = ReplayType::MSHR_HIT;
                resp.req_id = slot.req_id;
                COUNT_L1D_REPLAY(ctx, slot.req_id, false, slot.perf_replay,
                                 L1DReplayReason::WaitMshrHit);
            }
            else {
                // Miss with no existing MSHR entry: allocate a new MSHR and replay later when it is ready.
                if(mshr_free_entries == 0){
                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_FULL;
                    resp.req_id = slot.req_id;
                    COUNT_L1D_REPLAY(ctx, slot.req_id, false, slot.perf_replay,
                                     L1DReplayReason::MshrFull);
                }
                else { // The only free MSHR entry is being allocated by the other load port in the same cycle, so can't accept this new miss
                    out.dcache2mshr->mshr_req[i].valid = true;
                    out.dcache2mshr->mshr_req[i].addr = slot.addr;
#if !BSD_CONFIG
                    out.dcache2mshr->mshr_req[i].lsu_origin =
                        l1d_perf_is_lsu_req(slot.req_id);
#endif

                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_HIT;
                    resp.req_id = slot.req_id;
                    mshr_free_entries--;
                    COUNT_L1D_REPLAY(ctx, slot.req_id, false, slot.perf_replay,
                                     L1DReplayReason::WaitMshrFirstAlloc);
                }
            }
        } 
    }

    // ── Store ports ───────────────────────────────────────────────────────────
    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const S1S2Reg::StoreSlot &slot = s1s2_cur.stores[i];
        StoreResp &resp = out.dcache2lsu->resp_ports.store_resps[i];
        const int iidx = LSU_LDU_COUNT + i;

        if (!slot.valid) continue;

        AddrFields f          = decode(slot.addr);
        uint32_t tag_expected = f.tag;


        if(slot.replayed){
            resp.valid  = true;
            resp.replay = ReplayType::CONFLICT; // This store has been replayed due to MSHR full or conflict, so replay again to give chance for the MSHR state to be updated and avoid starvation when there are multiple back-to-back misses.
            resp.req_id = slot.req_id;
            COUNT_L1D_REPLAY(ctx, slot.req_id, true, slot.perf_replay,
                             slot.bank_conflict ? L1DReplayReason::BankConflict
                                                : L1DReplayReason::Conflict);
            continue;
        }        
        if(cache_line_match(slot.addr,in.mshr2dcache->fill_req.addr)&&in.mshr2dcache->fill_req.valid){ // merge the store into the incoming fill so the store can complete without replaying
            const AddrFields fill_req_f = decode(in.mshr2dcache->fill_req.addr);
            const bool fill_req_already_in_s2 =
                s1s2_cur.fill_write.valid &&
                s1s2_cur.fill_write.set_idx == fill_req_f.set_idx &&
                s1s2_cur.fill_write.tag == fill_req_f.tag;

            if (fill_req_already_in_s2 && out.fill_write->valid) {
                apply_strobe(out.fill_write->data[f.word_off], slot.data, slot.strb);
                out.fill_write->dirty = true;
            } else {
                fill_req_merge_wires[i].valid = true;
                fill_req_merge_wires[i].word_off = f.word_off;
                fill_req_merge_wires[i].data = slot.data;
                fill_req_merge_wires[i].strb = slot.strb;
            }

            resp.valid  = true;
            resp.replay = ReplayType::HIT;
            resp.req_id = slot.req_id;
            continue;
        }
        wire<DCACHE_WAY_BITS> hit_way = DCACHE_WAYS_NUM;
        for (int w = 0; w < DCACHE_WAYS_NUM; w++) {
            if (in.dcachelinereadresp[iidx]->valid[w] && in.dcachelinereadresp[iidx]->tag[w] == tag_expected) {
                hit_way = w;
                break;
            }
        }

        if (hit_way < DCACHE_WAYS_NUM) {
            if(s1s2_cur.fill_write.set_idx == f.set_idx && s1s2_cur.fill_write.valid&&out.fill_write->way_idx == hit_way){ // hit the line being filled in the same cycle, treat it as a hit and update the pending fill write to avoid a deadlock when MSHR is full
                resp.valid  = true;
                resp.replay = ReplayType::CONFLICT;
                resp.req_id = slot.req_id;
                COUNT_L1D_REPLAY(ctx, slot.req_id, true, slot.perf_replay,
                                 L1DReplayReason::WaitMshrFillWrite);
            }
            else{
                resp.valid  = true;
                resp.replay = ReplayType::HIT;
                resp.req_id = slot.req_id;
                *out.pendingwrite[i] = {
                    true,
                    f.set_idx,
                    hit_way,
                    f.word_off,
                    slot.data,
                    slot.strb
                };
                *out.lru_updates[LSU_LDU_COUNT + i] = {true, f.set_idx, hit_way};
            }
        }
        else {
            if(in.wb2dcache->merge_resp[i].valid){
                resp.valid = true;
                resp.replay = ReplayType::HIT;
                resp.req_id = slot.req_id;
            }
            else if(in.wb2dcache->merge_resp[i].busy){
                resp.valid = true;
                resp.replay = ReplayType::CONFLICT; // The store can proceed but the merge logic is busy, so replay later to give chance for the merge logic to catch up and avoid long latency on the following stores that hit the same line.
                resp.req_id = slot.req_id;
                COUNT_L1D_REPLAY(ctx, slot.req_id, true, slot.perf_replay,
                                 L1DReplayReason::Conflict);
            }
            else if(in.mshr2dcache->find_resp[iidx].valid){
                out.dcache2mshr->mshr_req[iidx].valid = true;
                out.dcache2mshr->mshr_req[iidx].addr = slot.addr;
                out.dcache2mshr->mshr_req[iidx].is_store = true;
                out.dcache2mshr->mshr_req[iidx].data = slot.data;
                out.dcache2mshr->mshr_req[iidx].strb = slot.strb;
#if !BSD_CONFIG
                out.dcache2mshr->mshr_req[iidx].lsu_origin =
                    l1d_perf_is_lsu_req(slot.req_id);
#endif
                resp.valid = true;
                resp.replay = ReplayType::HIT;
                resp.req_id = slot.req_id;
            }
            else{
                // Miss with no existing MSHR entry: allocate a new MSHR and replay later when it is ready.
                if(mshr_free_entries == 0){ // No free MSHR entry, or the only free entry is being allocated by the other store port in the same cycle, so can't accept this new miss
                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_FULL; // MSHR full, replay later
                    resp.req_id = slot.req_id;
                    COUNT_L1D_REPLAY(ctx, slot.req_id, true, slot.perf_replay,
                                     L1DReplayReason::MshrFull);
                }
                else {
                    out.dcache2mshr->mshr_req[iidx].valid = true;
                    out.dcache2mshr->mshr_req[iidx].addr = slot.addr;
                    out.dcache2mshr->mshr_req[iidx].is_store = true;
                    out.dcache2mshr->mshr_req[iidx].data = slot.data;
                    out.dcache2mshr->mshr_req[iidx].strb = slot.strb;
#if !BSD_CONFIG
                    out.dcache2mshr->mshr_req[iidx].lsu_origin =
                        l1d_perf_is_lsu_req(slot.req_id);
#endif

                    resp.valid = true;
                    resp.replay = ReplayType::HIT;
                    resp.req_id = slot.req_id;
                    mshr_free_entries--;
                }
            }
        }
    }

    out.dcache2lsu->mshr_fill = out.fill_write->valid; // Inform LSU about MSHR fill in the same cycle, so that it can prioritize the waiting load/store to consume the fill data and free up the MSHR entry as soon as possible, improving performance when there are back-to-back misses.
    out.dcache2lsu->mshr_fill_addr =
        out.fill_write->valid
            ? get_addr(out.fill_write->set_idx, out.fill_write->tag, 0)
            : 0;
}

void RealDcache::comb() {

    stage1_comb();

    stage2_comb();
}

// ─────────────────────────────────────────────────────────────────────────────
// seq — advance all state on the simulated clock edge.
// ─────────────────────────────────────────────────────────────────────────────
void RealDcache::seq() {
    // 1. Advance pipeline register.
    s1s2_cur = s1s2_nxt;
}
void RealDcache::dump_debug_state(FILE *out) const {
    fprintf(out, "RealDcache State:\n");
    fprintf(out, "  Stage1 Load Slots:\n");
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const S1S2Reg::LoadSlot &slot = s1s2_cur.loads[i];
        fprintf(out, "    Slot %d: Valid: %d, Addr: 0x%08x, Req ID: %u\n", i, slot.valid, slot.addr, slot.req_id);
    }
    fprintf(out, "  Stage1 Store Slots:\n");
    for (int i = 0; i < LSU_STA_COUNT; i++) {
        const S1S2Reg::StoreSlot &slot = s1s2_cur.stores[i];
        fprintf(out, "    Slot %d: Valid: %d, Addr: 0x%08x, Data: 0x%08x, Strb: 0x%02x, Req ID: %u\n", i, slot.valid, slot.addr, slot.data, slot.strb, slot.req_id);
    }
    fprintf(out, "  Stage1 Fill Write: Valid: %d, SetIdx: %u, Tag: 0x%08x, WayIdx: %u, Data: [", s1s2_cur.fill_write.valid, s1s2_cur.fill_write.set_idx, s1s2_cur.fill_write.tag, s1s2_cur.fill_write.way_idx);
    for (int w = 0; w < DCACHE_WORD_NUM; w++)    {
        fprintf(out, "0x%08x%s", s1s2_cur.fill_write.data[w], w == DCACHE_WORD_NUM - 1 ? "" : ", ");
    }
    fprintf(out, "]\n");
}
