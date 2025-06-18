#include <cstdint>
class Dcache;
class Memory;
union axi_ar_channel;
union axi_r_channel;
union axi_aw_channel;
union axi_b_channel;

struct axi_r_req_t {
    int mshr_entry;
    int word;
    uint8_t data[4];
};

struct mshr_info_master {
    bool cache_replace_out;
    bool cache_refill_out;
};

struct mshr_info_slave {
    bool cache_replace_in;
    bool cache_refill_in;
};

union mshr_info_t {
    struct mshr_info_master m;
    struct mshr_info_slave  s;
};

struct mshr_alloc_req_master {
    bool     valid_out;
    uint32_t tag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint8_t  wdata_aft_sft_out[4];
    bool     wdata_strb_out[4];
    bool     ok_in;
    int      mshr_entry_in;
};

struct mshr_alloc_req_slave {
    bool     valid_in;
    uint32_t tag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint8_t  wdata_aft_sft_in[4];
    bool     wdata_strb_in[4];
    bool     ok_out;
    int      mshr_entry_out;
};

union mshr_alloc_req_t {
    struct mshr_alloc_req_master m;
    struct mshr_alloc_req_slave  s;
};

struct refill_bus_master {
    bool valid_out;
    int mshr_entry_out;
    uint8_t refill_data_out[4][4];
};

struct refill_bus_slave {
    bool valid_in;
    int mshr_entry_in;
    uint8_t refill_data_in[4][4];
};

union refill_bus_t {
    struct refill_bus_master m;
    struct refill_bus_slave s;
};

struct mshr_entry {
    bool     valid;
    bool     fetch;
    bool     fetching;
    bool     fetched;
    bool     repl_issued;
    bool     wb2mem;
    bool     wb2meming;
    bool     wb2mem_ok;
    uint8_t  data2cache[4][4];
    uint8_t  strb2cache[4][4];
    uint8_t  repl_data[4][4];
    uint32_t repl_tag;
    uint32_t req_tag;
    int      repl_way;
    uint32_t req_idx;
    int      cnt;
};

class Mshr {
    public:
    union axi_ar_channel   *axi_ar;
    union axi_r_channel    *axi_r;
    union axi_aw_channel   *axi_aw;
    union axi_b_channel    *axi_b;
    union refill_bus_t     *refill_bus;
    union mshr_info_t      *mshr_info;
    union mshr_alloc_req_t *mshr_alc_req;
    struct axi_r_req_t      axi_r_req;

    struct mshr_entry mshr_r[4];
    struct mshr_entry mshr_r_io[4];
    int  head_r;
    int  head_r_io;
    int  retire_r;
    int  retire_r_io;
    int  tail_r;
    int  tail_r_io;
    bool repl_r;
    bool repl_r_io;
    int  repl_entry_r;
    int  repl_entry_r_io;

    int fetch_ent;

    Dcache      *dcache;
    Memory      *memory;

    public:
    void default_val();
    bool raw_check(int);
    void free();
    void alloc();
    void fetch_forepart();
    void fetch_backpart();
    void axi_r_func();
    void wb2mem();
    void wb2mem_ok();
    void replace();
    void refill();
    void update_data2cache();
    void seq();
};