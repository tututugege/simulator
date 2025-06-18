#include "MemTop.h"

void Mem_Top::init_module() {
    lsu            = new LSU();
    load_queue     = new Load_Queue();
    store_queue    = new Store_Queue();
    dcache         = new Dcache();
    mshr           = new Mshr();
    mmu            = new MMU();
    dcache_req_arb = new Dcache_Req_Arb();
    addr_trans_arb = new Addr_Trans_Arb();
    memory         = new Memory();
}

void Mem_Top::conn_module() {
    addr_trans_arb->rs_trans_req = &rs_trans_req;
    load_queue->ldq_trans_req = &ldq_trans_req;
    addr_trans_arb->ldq_trans_req = &ldq_trans_req;
    store_queue->stq_trans_req = &stq_trans_req;
    addr_trans_arb->stq_trans_req = &stq_trans_req;
    addr_trans_arb->out_trans_req = &out_trans_req;
    lsu->out_trans_req = &out_trans_req;

    lsu->lsu_cache_req = &lsu_cache_req;
    dcache_req_arb->lsu_cache_req = &lsu_cache_req;
    load_queue->ldq_cache_req = &ldq_cache_req;
    dcache_req_arb->ldq_cache_req = &ldq_cache_req;
    store_queue->stq_cache_req = &stq_cache_req;
    dcache_req_arb->stq_cache_req = &stq_cache_req;
    dcache_req_arb->cache_req = &cache_req;
    dcache->cache_req = &cache_req;
    lsu->mmu_req = &mmu_req;
    mmu->mmu_req = &mmu_req;

    lsu->mmu_resp = &mmu_resp;
    mmu->mmu_resp = &mmu_resp;
    lsu->ldq_fill_req = &ldq_fill_req;
    load_queue->ldq_fill_req = &ldq_fill_req;
    lsu->stq_fill_req = &stq_fill_req;
    store_queue->stq_fill_req = &stq_fill_req;
    load_queue->stq_fwd_req = &stq_fwd_req;
    store_queue->stq_fwd_req = &stq_fwd_req;
    store_queue->stq_fwd_data = &stq_fwd_data;
    load_queue->bcast_bus = &bcast_bus;

    dcache->mmu_resp = &mmu_resp;

    dcache->cache_res = &cache_res;
    load_queue->cache_res = &cache_res;
    store_queue->cache_res = &cache_res;
    dcache->mshr_alc_req = &mshr_alloc_req;
    mshr->mshr_alc_req = &mshr_alloc_req;
    mshr->mshr_info = &mshr_info;
    dcache->mshr_info = &mshr_info;

    mshr->refill_bus = &refill_bus;
    load_queue->refill_bus = &refill_bus;
    store_queue->refill_bus = &refill_bus;

    memory->axi_ar = &axi_ar;
    mshr->axi_ar = &axi_ar;
    memory->axi_r = &axi_r;
    mshr->axi_r = &axi_r;
    memory->axi_aw = &axi_aw;
    mshr->axi_aw = &axi_aw;
    memory->axi_b = &axi_b;
    mshr->axi_b = &axi_b;
}

void Mem_Top::default_val() {
    rs_trans_req.valid_in  = false;
    ldq_trans_req.m.valid_out = false;
    stq_trans_req.m.valid_out = false;

    lsu_cache_req.m.valid_out = false;
    ldq_cache_req.m.valid_out = false;
    stq_cache_req.m.valid_out = false;
    mmu_req.m.valid_out = false;

    ldq_fill_req.m.valid_out = false;
    stq_fill_req.m.valid_out = false;

    cache_res.m.valid_out = false;
    mshr_alloc_req.m.valid_out = false;

    mshr_info.m.cache_replace_out = false;
    mshr_info.m.cache_refill_out = false;
    refill_bus.m.valid_out = false;

    axi_ar.m.arvalid_out = false;
    axi_r.m.rvalid_out = false;
    axi_aw.m.awvalid_out = false;
    axi_b.m.bvalid_out = false;
}

void Mem_Top::dispatch() {
    load_queue->alloc();
    store_queue->alloc();
}

void Mem_Top::comb() {
    load_queue->free();
    store_queue->free();
    store_queue->retire();
    mshr->fetch_forepart();
    memory->ar_func();
    mshr->fetch_backpart();
    mshr->axi_r_func();
    mshr->wb2mem();
    mshr->wb2mem_ok();
    mshr->free();
    mshr->replace();
    mshr->refill();
    load_queue->recv_refill_data();
    load_queue->bcast_res_bus();
    store_queue->recv_refill_data();
    load_queue->addr_trans_req_forepart();
    store_queue->addr_trans_req_forepart();
    addr_trans_arb->arbit_req_forepart();
    lsu->stage1_forepart();
    load_queue->fire_ld2cache_forepart();
    store_queue->fire_st2cache_forepart();
    dcache_req_arb->arbit_req_forepart();
    dcache->stage3_forepart();
    mshr->alloc();
    dcache->stage3_backpart();
    load_queue->recv_cache_res();
    store_queue->recv_cache_res();
    mshr->update_data2cache();
    dcache->stage1();
    dcache_req_arb->arbit_req_backpart();
    lsu->stage1_backpart();
    load_queue->fire_ld2cache_backpart();
    store_queue->fire_st2cache_backpart();
    mmu->req();
    addr_trans_arb->arbit_req_backpart();
    load_queue->addr_trans_req_backpart();
    store_queue->addr_trans_req_backpart();
    mmu->resp();
    lsu->stage2();
    load_queue->fill_addr();
    store_queue->fill_addr();
    load_queue->fwd_req();
    store_queue->fwd_handler();
    load_queue->fill_fwd_data();
    dcache->stage2(); 
}

void Mem_Top::seq() {
    lsu->seq();
    load_queue->seq();
    store_queue->seq();
    dcache->seq();
    memory->seq();
}