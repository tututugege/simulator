#include <cstdint>

struct rs_trans_req_slave {
    bool     valid_in;
    uint8_t  op_in;
    uint8_t  mem_sz_in;
    uint32_t rs1_in;
    uint32_t rs2_in;
    uint8_t  wdata_b4_sft_in[4];
    int      lsq_entry_in;
    bool     ready_out; 
};

struct ldq_trans_req_master {
    bool     valid_out;
    uint32_t vtag_out;
    int      ldq_entry_out;
    bool     ready_in;
};

struct ldq_trans_req_slave {
    bool     valid_in;
    uint32_t vtag_in;
    int      ldq_entry_in;
    bool     ready_out;
};

union ldq_trans_req_t {
    struct ldq_trans_req_master m;
    struct ldq_trans_req_slave  s;
};

struct stq_trans_req_master {
    bool     valid_out;
    uint32_t vtag_out;
    int      stq_entry_out;
    bool     ready_in;
};

struct stq_trans_req_slave {
    bool     valid_in;
    uint32_t vtag_in;
    int      stq_entry_in;
    bool     ready_out;
};

union stq_trans_req_t {
    struct stq_trans_req_master m;
    struct stq_trans_req_slave  s;
};

struct out_trans_req_master {
    bool     valid_out;
    uint8_t  op_out;
    uint8_t  src_out;
    uint8_t  mem_sz_out;
    uint32_t rs1_out;
    uint32_t rs2_out;
    uint32_t vtag_out;
    uint32_t index_out;
    uint32_t word_out;
    uint32_t offset_out;
    uint8_t  wdata_b4_sft_out[4];
    int      lsq_entry_out;
    bool     ready_in; 
};

struct out_trans_req_slave {
    bool     valid_in;
    uint8_t  op_in;
    uint8_t  src_in;
    uint8_t  mem_sz_in;
    uint32_t rs1_in;
    uint32_t rs2_in;
    uint32_t vtag_in;
    uint32_t index_in;
    uint32_t word_in;
    uint32_t offset_in;
    uint8_t  wdata_b4_sft_in[4];
    int      lsq_entry_in;
    bool     ready_out; 
};

union out_trans_req_t {
    struct out_trans_req_master m;
    struct out_trans_req_slave  s;
};

class Addr_Trans_Arb {
    public:
    struct rs_trans_req_slave *rs_trans_req;
    union ldq_trans_req_t *ldq_trans_req;
    union stq_trans_req_t *stq_trans_req;
    union out_trans_req_t *out_trans_req;

    public:
    void default_val();
    void arbit_req_forepart();
    void arbit_req_backpart();
};