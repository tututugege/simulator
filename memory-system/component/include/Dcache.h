#include <cstdint>
union mshr_alloc_req_t;
union mshr_info_t;
union mmu_resp_t;

struct cache_req_master {
    bool     valid_out;
    uint8_t  op_out;
    bool     tagv_out;
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint32_t offset_out;
    uint8_t  wdata_aft_sft_out[4];
    bool     wstrb_out[4];
    int      lsq_entry_out;
    bool     addr_ok_in;
};

struct cache_req_slave {
    bool     valid_in;
    uint8_t  op_in;
    bool     tagv_in;
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint32_t offset_in;
    uint8_t  wdata_aft_sft_in[4];
    bool     wstrb_in[4];
    int      lsq_entry_in;
    bool     addr_ok_out;
};

union cache_req_t {
    struct cache_req_master m;
    struct cache_req_slave s;
};

struct cache_req_info_t {
    bool     valid;
    bool     abort;
    uint8_t  op;
    bool     tagv;
    uint32_t tag;
    uint32_t index;
    uint32_t word;
    uint32_t offset;
    uint8_t  wdata_aft_sft[4];
    bool     wstrb[4];
    int      lsq_entry;
};

struct cache_hit_info_t {
    bool    hit;
    int     hit_way;
    uint8_t rdata[4];
};

struct cache_fwd_info_t {
    uint8_t fwd_byte[4];
    bool    fwd_strb[4];
};

struct cache_res_master {
    bool    valid_out;
    uint8_t op_out;
    bool    hit_out;
    bool    miss_out;
    bool    abort_out;
    uint8_t data_out[4];
    int     mshr_entry_out;
    int     lsq_entry_out;
};

struct cache_res_slave {
    bool    valid_in;
    uint8_t op_in;
    bool    hit_in;
    bool    miss_in;
    bool    abort_in;
    uint8_t data_in[4];
    int     mshr_entry_in;
    int     lsq_entry_in;
};

union cache_res_t {
    struct cache_res_master m;
    struct cache_res_slave s;
};

struct tagv_ram_entry_t {
    bool v;
    uint32_t tag;
};

struct data_ram_entry_t {
    uint8_t data[4];
};

struct Block_ram_io {
    bool en;
    bool we;
    uint32_t addr;
    bool v;
    uint32_t tag;
};

struct Byte_ram_io {
    bool en;
    bool we[4];
    uint32_t addr;
    uint8_t  din[4];
};

class Dcache {
    public:
    union cache_req_t      *cache_req;
    union cache_res_t      *cache_res;
    union mshr_alloc_req_t *mshr_alc_req;
    union mshr_info_t      *mshr_info;
    union mmu_resp_t       *mmu_resp;

    struct Block_ram_io tagv_ram_io[2];
    struct tagv_ram_entry_t tagv_ram[2][8];
    struct Byte_ram_io data_ram_io[2][4];
    struct data_ram_entry_t data_ram[2][4][8];
    bool dirty_r_io[4][8];
    bool dirty_r[4][8];
    struct cache_req_info_t req_info_r2;
    struct cache_req_info_t req_info_r2_io;
    struct cache_req_info_t req_info_r3;
    struct cache_req_info_t req_info_r3_io;
    struct cache_hit_info_t hit_info_r3;
    struct cache_hit_info_t hit_info_r3_io;
    struct cache_fwd_info_t fwd_info_r3;
    struct cache_fwd_info_t fwd_info_r3_io;

    bool abort_w3;

    public:
    void default_val();
    void stage1();
    void stage2();
    void stage3_forepart();
    void stage3_backpart();
    void seq();
};