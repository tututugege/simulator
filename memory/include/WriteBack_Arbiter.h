#pragma once
#include "IO.h"
#include  <config.h>

class WriteBack_Arbiter_IN {
public:
    MemRespIO* mshr_resp;
    MemRespIO* dcache_st_resp;
    MemRespIO* dcache_ld_resp;
};
class WriteBack_Arbiter_OUT {
public:
    MemRespIO* ld_resp;
    MemRespIO* st_resp;
    WbArbiterDcacheIO* wb_arbiter2dcache;
};
class WriteBack_Arbiter {
public:
    WriteBack_Arbiter_IN in;
    WriteBack_Arbiter_OUT out;

    void seq();
    void comb();
    void print();
};