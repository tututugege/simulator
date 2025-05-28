#pragma once
#include "LSU.h"
#include "util.h"

struct rs_trans_req_master {
    bool     valid_out;
    op_t     op_out;
    mem_sz_t mem_sz_out;
    uint32_t rs1;
    uint32_t rs2;
    uint8_t  wdata_b4_sft_out[4];
    int      lsq_entry_out;
    bool     ready_in; 
};

struct rs_trans_req_slave {
    bool     valid_in;
    op_t     op_in;
    mem_sz_t mem_sz_in;
    uint32_t rs1;
    uint32_t rs2;
    uint8_t  wdata_b4_sft_in[4];
    int      lsq_entry_in;
    bool     ready_out; 
};

union rs_trans_req_t {
    struct rs_trans_req_master m;
    struct rs_trans_req_slave  s;
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
    op_t     op_out;
    src_t    src_out;
    mem_sz_t mem_sz_out;
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
    op_t     op_in;
    src_t    src_in;
    mem_sz_t mem_sz_in;
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
    union rs_trans_req_t  *rs_trans_req;
    union ldq_trans_req_t *ldq_trans_req;
    union stq_trans_req_t *stq_trans_req;
    union out_trans_req_t *out_trans_req;

    LSU * lsu;

    public:
    void default_val();
    void arbit_req_forepart();
    void arbit_req_backpart();
};