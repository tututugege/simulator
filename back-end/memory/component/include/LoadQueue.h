#pragma once
#include "AddrTransArb.h"
#include "Dcache.h"
#include "Mshr.h"
#include "IO.h"

struct ldq_fill_req_master {
    bool     valid_out;
    src_t    src_out;
    int      ldq_entry_out;
    bool     addr_trans_out;
    bool     paddrv_out;
    bool     paddr_out;
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint32_t offset_out;
};

struct ldq_fill_req_slave {
    bool     valid_in;
    src_t    src_in;
    int      ldq_entry_in;
    bool     addr_trans_in;
    bool     paddrv_in;
    bool     paddr_in;
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint32_t offset_in;
};

union ldq_fill_req_t {
    struct ldq_fill_req_master m;
    struct ldq_fill_req_slave s;
};

struct ldq_entry_t {
    bool     valid;
    bool     abort;
    bool     done;
    bool     miss;
    bool     bcast;
    bool     retire; // 检查store to load 为例，在load指令退休前不能释放ldq表项
    bool     addr_trans;
    bool     paddrv;
    uint32_t tag;
    uint32_t index;
    uint32_t word;
    uint32_t offset;
    mem_sz_t mem_sz;
    uint8_t  data_b4_bcast[4];
    bool     fwd_strb[4];
    int      mshr_entry;
    int      dst_preg;
    bool     sign;
    int      id;
};

class Load_Queue {
    public:
    union ldq_trans_req_t *ldq_trans_req;
    union cache_req_t     *ldq_cache_req;
    union cache_res_t     *cache_res;
    union ldq_fill_req_t  *ldq_fill_req;
    union stq_fwd_req_t   *stq_fwd_req;
    union refill_bus_t    *refill_bus;
    union stq_fwd_data_t  *stq_fwd_data;

    struct bcast_res_master *bcast_bus;
    Ren_Lsu *ren2lsu;
    Lsu_Ren *lsu2ren;
    Rob_Lsq *rob2ldq;

    struct ldq_entry_t ldq[8];
    struct ldq_entry_t ldq_io[8];
    int head_r;
    int head_r_io;
    int tail_r;
    int tail_r_io;
    int addr_trans_entry;

    public:
    void default_val();
    void alloc();
    void free();
    void addr_trans_req_forepart();
    void addr_trans_req_backpart();
    void fire_ld2cache_forepart();
    void fire_ld2cache_backpart();
    void fwd_req();
    void fill_addr();
    void fill_fwd_data();
    void recv_cache_res();
    void recv_refill_data();
    void bcast_res_bus();
    void seq();
};