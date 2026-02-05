#pragma once
#include "IO.h"
#include "mmu_io.h"
#include <config.h>
#include <cmath>
#include "Dcache_Utils.h"

class Dcache_IN {
public:
    MemReqIO* ldq2dcache_req;
    MemReqIO* stq2dcache_req;

    DcacheControlIO* control;
    WbArbiterDcacheIO* wb_arbiter2dcache;
    WritebufferDcacheIO* wb2dcache;

    MemReadyIO* mshr2dcache_ready;

    MshrFwdIO* mshr2dcache_fwd;
#if defined(CONFIG_MMU)&& defined(CONFIG_CACHE)
    dcache_resp_slave_t *ptw2dcache_resp;
    dcache_req_master_t *ptw2dcache_req;
#endif
};
class Dcache_OUT {
public:
    MemRespIO* dcache2ldq_resp;
    MemRespIO* dcache2stq_resp;
    MemReadyIO* dcache2ldq_ready;
    MemReadyIO* dcache2stq_ready;

    DcacheMshrIO* dcache2mshr_ld;
    DcacheMshrIO* dcache2mshr_st;
#if defined(CONFIG_MMU)&& defined(CONFIG_CACHE)
    dcache_req_slave_t *dcache2ptw_req;
    dcache_resp_master_t *dcache2ptw_resp;
#endif
};

typedef struct 
{
    bool valid;
    uint32_t addr;
    uint32_t tag;
    uint32_t index;
    uint32_t wdata;
    uint8_t wstrb;
    InstUop uop;
}Pipe_Reg;

class Dcache {
public:
    Dcache_IN in;
    Dcache_OUT out;


    Pipe_Reg s1_reg_ld,s2_reg_ld;
    Pipe_Reg s1_reg_st,s2_reg_st;
    uint32_t tag_reg_ld[DCACHE_WAY_NUM];
    uint32_t tag_reg_st[DCACHE_WAY_NUM];
    uint32_t data_reg_ld[DCACHE_WAY_NUM];
    uint32_t data_reg_st[DCACHE_WAY_NUM];

    Pipe_Reg s1_next_ld,s2_next_ld;
    Pipe_Reg s1_next_st,s2_next_st;
    uint32_t tag_next_ld[DCACHE_WAY_NUM];
    uint32_t tag_next_st[DCACHE_WAY_NUM];
    uint32_t data_next_ld[DCACHE_WAY_NUM];
    uint32_t data_next_st[DCACHE_WAY_NUM];

    uint32_t mmu_reg_tag[DCACHE_WAY_NUM];
    uint32_t mmu_reg_data[DCACHE_WAY_NUM];

    uint32_t mmu_next_tag[DCACHE_WAY_NUM];
    uint32_t mmu_next_data[DCACHE_WAY_NUM];

    void comb_out_ldq();
    void comb_out_mshr();
    void comb_out_ready();
    void comb_s1();
    void comb_s2();
    void comb_mmu();
    void seq();

    void init();
    void print();
};
