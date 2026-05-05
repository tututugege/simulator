#include "RealDcache.h"
#include <cassert>
#include <cstring>
#include <MemUtils.h>

void RealDcache::init() {
    init_dcache();
    s1s2_cur = {};
    s1s2_nxt = {};
}

void RealDcache::stage1_comb() {

    AddrFields fill_f = decode(in.mshr2dcache->fill_req.addr);
    
    bool same_fill_already_in_s2 =
    s1s2_cur.fill_write.valid &&
    in.mshr2dcache->fill_req.valid &&
    s1s2_cur.fill_write.set_idx == fill_f.set_idx &&
    s1s2_cur.fill_write.tag == fill_f.tag;

    
    s1s2_nxt.fill_write.valid = in.mshr2dcache->fill_req.valid && !same_fill_already_in_s2;
    s1s2_nxt.fill_write.set_idx = fill_f.set_idx;
    s1s2_nxt.fill_write.tag = fill_f.tag;
    memcpy(s1s2_nxt.fill_write.data,
        in.mshr2dcache->fill_req.data,
        sizeof(s1s2_nxt.fill_write.data));


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
            out.dcache2wb->bypass_req[i].valid = false;
        }
        else{
            bool replay = false;
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
            slot.replayed  = replay;
            AddrFields f  = decode(req.addr);
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
            out.dcache2wb->merge_req[i].valid = false;
        }
        else{
            bool replayed = false;
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
            slot.replayed = replayed;
            AddrFields f  = decode(req.addr);
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

    out.fillout->valid = in.mshr2dcache->fill_req.valid;
    out.fillout->set_idx = decode(in.mshr2dcache->fill_req.addr).set_idx;

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

    if(s1s2_cur.fill_write.valid){
        int select_way = choose_plru_tree_victim(in.fillin->plru_tree_state, in.fillin->valid_snap);
        if(in.fillin->valid_snap[select_way] && in.fillin->dirty_snap[select_way]){
            if(in.wb2dcache->free>0){
                out.dcache2wb->dirty_info.valid = true;
                out.dcache2wb->dirty_info.addr = get_addr(s1s2_cur.fill_write.set_idx, in.fillin->tag_snap[select_way], 0);
                memcpy(out.dcache2wb->dirty_info.data, in.fillin->data_snap[select_way], DCACHE_WORD_NUM * sizeof(uint32_t));

                out.fill_write->valid = true;
                out.fill_write->set_idx = s1s2_cur.fill_write.set_idx;
                out.fill_write->tag = s1s2_cur.fill_write.tag;
                out.fill_write->way_idx = select_way;
                memcpy(out.fill_write->data, s1s2_cur.fill_write.data, sizeof(out.fill_write->data));
                out.dcache2mshr->fill_resp.done = true; // Mark the MSHR fill as done to unblock the waiting load/store, even though the line is not yet filled into the cache. This allows the writeback to be issued in parallel with the fill, improving performance when the evicted line is dirty.
            }
            else{
                out.fill_write->valid = false; // Can't accept new fill, so stall the MSHR fill response and the waiting load/store. This may cause deadlock when MSHR and WB are both full, but it's a rare case and can be resolved by replaying the load/store.
                out.dcache2wb->dirty_info.valid = false; // Can't accept new writeback, so treat the line as clean and let it be overwritten without writeback. This may cause data loss but prevents deadlock when MSHR and WB are both full.
            }
        }
        else{
            out.fill_write->valid = true;
            out.fill_write->set_idx = s1s2_cur.fill_write.set_idx;
            out.fill_write->tag = s1s2_cur.fill_write.tag;
            out.fill_write->way_idx = select_way;
            memcpy(out.fill_write->data, s1s2_cur.fill_write.data, sizeof(out.fill_write->data));
            out.dcache2mshr->fill_resp.done = true; // Mark the MSHR fill as done to unblock the waiting load/store, even though the line is not yet filled into the cache. This allows the fill to complete and the line to be allocated in cache without waiting for an unnecessary writeback to drain when the evicted line is clean.
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
            if(s1s2_cur.icache_req==i)continue;
            if(in.mshr2dcache->find_resp[i].valid){
                resp.valid = true;
                resp.replay = ReplayType::MSHR_HIT;
                resp.req_id = slot.req_id;
            }
            else {
                // Miss with no existing MSHR entry: allocate a new MSHR and replay later when it is ready.
                if(mshr_free_entries == 0){
                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_FULL;
                    resp.req_id = slot.req_id;
                }
                else if(!out.dcache2mshr->mshr_req[0].valid){ // The only free MSHR entry is being allocated by the other load port in the same cycle, so can't accept this new miss
                    out.dcache2mshr->mshr_req[0].valid = true;
                    out.dcache2mshr->mshr_req[0].addr = slot.addr;

                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_HIT;
                    resp.req_id = slot.req_id;
                }
                else{
                    resp.valid = true;
                    resp.replay = ReplayType::CONFLICT; 
                    resp.req_id = slot.req_id;
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
            continue;
        }        
        if(cache_line_match(slot.addr,in.mshr2dcache->fill_req.addr)&&in.mshr2dcache->fill_req.valid){ // hit the MSHR entry allocated in the same cycle, treat it as a hit and update the pending MSHR entry to avoid a deadlock when MSHR is full
           resp.valid  = true;
           resp.replay = ReplayType::CONFLICT;
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
            }
            else if(in.mshr2dcache->find_resp[iidx].valid){
                resp.valid = true;
                resp.replay = ReplayType::MSHR_HIT;
                resp.req_id = slot.req_id;
            }
            else{
                // Miss with no existing MSHR entry: allocate a new MSHR and replay later when it is ready.
                if(mshr_free_entries == 0 || (mshr_free_entries == 1 && out.dcache2mshr->mshr_req[0].valid)){ // No free MSHR entry, or the only free entry is being allocated by the other store port in the same cycle, so can't accept this new miss
                    resp.valid = true;
                    resp.replay = ReplayType::MSHR_FULL; // MSHR full, replay later
                    resp.req_id = slot.req_id;
                }
                else if(!out.dcache2mshr->mshr_req[1].valid){ // The free MSHR entry is available for allocation
                    if(out.dcache2mshr->mshr_req[0].valid&&cache_line_match(slot.addr,out.dcache2mshr->mshr_req[0].addr)){ // The only free MSHR entry is being allocated by the other store port in the same cycle, so can't accept this new miss
                        resp.valid = true;
                        resp.replay = ReplayType::MSHR_HIT;
                        resp.req_id = slot.req_id;
                    }else{
                        out.dcache2mshr->mshr_req[1].valid = true;
                        out.dcache2mshr->mshr_req[1].addr = slot.addr;

                        resp.valid = true;
                        resp.replay = ReplayType::MSHR_HIT;
                        resp.req_id = slot.req_id;
                    }
                }
                else{
                    resp.valid = true;
                    resp.replay = ReplayType::CONFLICT; // MSHR conflict with the other port in the same cycle, replay later
                    resp.req_id = slot.req_id;
                }
            }
        }
    }

    out.dcache2lsu->mshr_fill = out.fill_write->valid; // Inform LSU about MSHR fill in the same cycle, so that it can prioritize the waiting load/store to consume the fill data and free up the MSHR entry as soon as possible, improving performance when there are back-to-back misses.
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
