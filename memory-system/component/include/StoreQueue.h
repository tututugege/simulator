#pragma once
#include "AddrTransArb.h"
#include "Dcache.h"
#include "Mshr.h"

struct stq_fwd_req_master {
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    bool byte_mask_out[4];
};

struct stq_fwd_req_slave {
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    bool byte_mask_in[4];
};

union stq_fwd_req_t {
    struct stq_fwd_req_master m;
    struct stq_fwd_req_slave s;
};

struct stq_fwd_data_master {
    uint8_t fwd_byte_out[4];
    bool    fwd_strb_out[4];
};

struct stq_fwd_data_slave {
    uint8_t fwd_byte_in[4];
    bool    fwd_strb_in[4];
};

union stq_fwd_data_t {
    struct stq_fwd_data_master m;
    struct stq_fwd_data_slave s;
};

struct stq_fill_req_master {
    bool     valid_out;
    int      stq_entry_out;
    src_t    src_out;
    bool     addr_trans_out;
    bool     paddrv_out;
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint32_t offset_out;
    uint8_t  wdata_b4_sft_out[4];
};

struct stq_fill_req_slave {
    bool     valid_in;
    int      stq_entry_in;
    src_t    src_in;
    bool     addr_trans_in;
    bool     paddrv_in;
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint32_t offset_in;
    uint8_t  wdata_b4_sft_in[4];
};

union stq_fill_req_t {
    struct stq_fill_req_master m;
    struct stq_fill_req_slave s;
};

struct stq_entry_t {
    bool     valid;
    bool     fired;
    bool     done;
    bool     miss;
    bool     addr_trans;
    bool     paddrv;
    uint32_t tag;
    uint32_t index;
    uint32_t word;
    mem_sz_t mem_sz;
    uint8_t  wdata_aft_sft[4];
    bool     wstrb[4];
    int      mshr_entry;
};

class Store_Queue {
    public:
    union stq_trans_req_t *stq_trans_req;
    union stq_fwd_req_t   *stq_fwd_req;
    union stq_fwd_data_t  *stq_fwd_data;
    union stq_fill_req_t  *stq_fill_req;
    union cache_req_t     *stq_cache_req;
    union cache_res_t     *cache_res;
    union refill_bus_t    *refill_bus;

    struct stq_entry_t stq[8];
    struct stq_entry_t stq_io[8];
    int head_r;
    int head_r_io;
    int retire_r;
    int retire_r_io;
    int tail_r;
    int tail_r_io;
    int addr_trans_entry;

    public:
    void free();
    void alloc();
    void retire();
    void addr_trans_req_forepart();
    void addr_trans_req_backpart();
    void fire_st2cache_forepart();
    void fire_st2cache_backpart();
    void fwd_handler();
    void fill_addr();
    void recv_cache_res();
    void recv_refill_data();
    void default_val();
    void seq();
};