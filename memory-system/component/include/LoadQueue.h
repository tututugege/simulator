#include <cstdint>
union ldq_trans_req_t;
union cache_req_t;
union cache_res_t;
union stq_fwd_req_t;
union refill_bus_t;
union stq_fwd_data_t;

struct ldq_fill_req_master {
    bool     valid_out;
    uint8_t  src_out;
    int      ldq_entry_out;
    bool     addr_trans_out;
    bool     paddrv_out;
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint32_t offset_out;
};

struct ldq_fill_req_slave {
    bool     valid_in;
    uint8_t  src_in;
    int      ldq_entry_in;
    bool     addr_trans_in;
    bool     paddrv_in;
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint32_t offset_in;
};

union ldq_fill_req_t {
    struct ldq_fill_req_master m;
    struct ldq_fill_req_slave s;
};

struct bcast_res_bus_master {
    bool    valid_out;
    uint8_t data_out[4];
    int     dst_preg_out;
    bool    ready_in;
};

struct ldq_alloc_slave {
    bool    valid_in;
    uint8_t mem_sz_in;
    uint8_t dst_preg_in;
    bool    sign_in;
    bool    ready_out;
    int     ldq_idx_out;
};

struct ldq_entry_t {
    bool     valid;
    bool     abort;
    bool     done;
    bool     miss;
    bool     bcast;
    bool     addr_trans;
    bool     paddrv;
    uint32_t tag;
    uint32_t index;
    uint32_t word;
    uint32_t offset;
    uint8_t  data_b4_bcast[4];
    bool     fwd_strb[4];
    int      mshr_entry;
    uint8_t  mem_sz; // 0 byte 1 half word 2 word
    uint8_t  dst_preg;
    bool     sign;
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
    struct bcast_res_bus_master *bcast_bus;
    struct ldq_alloc_slave *ldq_alloc;

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