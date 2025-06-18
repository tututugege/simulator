#include "Dcache.h"
#include "Mshr.h"
#include "AddrTransArb.h"
#include "MMU.h"
#include "LoadQueue.h"
#include "StoreQueue.h"
#include "DcacheReqArb.h"
#include "Memory.h"
#include "LSU.h"

class Mem_Top {
    public:
    union cache_req_t lsu_cache_req;
    union cache_req_t ldq_cache_req;
    union cache_req_t stq_cache_req;
    union cache_req_t cache_req;
    union cache_res_t cache_res;

    union mshr_alloc_req_t mshr_alloc_req;
    union mshr_info_t      mshr_info;
    union refill_bus_t     refill_bus;
    struct bcast_res_bus_master bcast_bus;

    struct rs_trans_req_slave rs_trans_req;
    union ldq_trans_req_t ldq_trans_req;
    union stq_trans_req_t stq_trans_req;
    union out_trans_req_t out_trans_req;

    union mmu_req_t      mmu_req;
    union mmu_resp_t     mmu_resp;
    union stq_fwd_req_t  stq_fwd_req;
    union stq_fwd_data_t stq_fwd_data;
    union ldq_fill_req_t ldq_fill_req;
    union stq_fill_req_t stq_fill_req;

    union axi_ar_channel axi_ar;
    union axi_r_channel  axi_r;
    union axi_aw_channel axi_aw;
    union axi_b_channel  axi_b;

    LSU            *lsu;
    Load_Queue     *load_queue;
    Store_Queue    *store_queue;
    Dcache         *dcache;
    Mshr           *mshr;
    Dcache_Req_Arb *dcache_req_arb;
    Addr_Trans_Arb *addr_trans_arb;
    Memory         *memory;
    MMU            *mmu;

    public:
    void init_module();
    void conn_module();
    void default_val();
    void dispatch();
    void comb();
    void seq();
};