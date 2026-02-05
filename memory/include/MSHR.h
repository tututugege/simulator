#pragma once
#include "IO.h"
#include "Dcache_Utils.h"
#include <config.h>
#include <cstdint>

const int MSHR_ENTRY_SIZE = 8;
const int MSHR_TABLE_SIZE = 16;

typedef struct{
    bool valid;
    bool issued;
    uint32_t index;
    uint32_t tag;
    uint32_t count;
}mshr_entry;

typedef struct{
    bool valid;
    uint32_t entry;
    uint32_t type;
    uint32_t offset;
    uint32_t reg;
    uint32_t wstrb;
    uint32_t wdata;
    
    InstUop uop;
}table_entry;

enum MSHR_STATE{
    MSHR_IDLE,
    MSHR_DEAL,
    MSHR_TRAN,
    MSHR_WRITEBACK,
    MSHR_FORWARD
};


class MSHR_IN {
public:
    DcacheMshrIO* dcache2mshr_ld;
    DcacheMshrIO* dcache2mshr_st;    
    DcacheControlIO* control;

    ExmemDataIO* arbiter2mshr_data;

    WbMshrIO* writebuffer2mshr;

};
class MSHR_OUT {
public:
    MemRespIO* mshr2cpu_resp;
    MemReadyIO* mshr2dcache_ready;
    ExmemControlIO* mshr2arbiter_control;

    MshrWbIO* mshr2writebuffer;

    MshrArbiterIO* mshr2arbiter;

    MshrFwdIO* mshr2dcache_fwd;

};

class MSHR {
public:
    void init();
    void comb_out();
    void comb_ready();
    void seq();
    void comb();

    uint32_t mshr_head;
    uint32_t mshr_tail;
    uint32_t table_head;
    uint32_t table_tail;
    uint32_t count_mshr;
    uint32_t count_table;
    uint32_t count_data;
    uint32_t done_type = 0;
    uint32_t deal_index = 0;

    MSHR_IN in;
    MSHR_OUT out;
    void print();
    void table_free(uint32_t idx);
    uint32_t find_entry(uint32_t addr);
    void entry_add(uint32_t idx,uint32_t index,uint32_t tag);
    void table_add(uint32_t idx,bool type,uint32_t offset,uint32_t reg,uint32_t wstrb,uint32_t wdata,InstUop uop);
};
