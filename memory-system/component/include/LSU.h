#pragma once
#include "Dcache.h"
#include "MMU.h"

struct lsu_info_t {
    bool     valid;
    op_t     op;
    src_t    src;
    mem_sz_t mem_sz;
    uint32_t vtag;
    uint32_t index;
    uint32_t word;
    uint32_t offset;
    uint8_t  wdata_b4_sft[4];
    int      lsq_entry;
};

class LSU {
    public:
    union out_trans_req_t  *out_trans_req;
    union mmu_req_t        *mmu_req;
    union mmu_resp_t       *mmu_resp;
    union cache_req_t      *lsu_cache_req;
    union ldq_fill_req_t   *ldq_fill_req;
    union stq_fill_req_t   *stq_fill_req;

    struct lsu_info_t stage2_info_r;
    struct lsu_info_t stage2_info_r_io;

    public:
    void default_val();
    void stage1_forepart();
    void stage1_backpart();
    void stage2();
    void seq();
};

