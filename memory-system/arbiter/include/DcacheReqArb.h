#pragma once
#include "Dcache.h"

class Dcache_Req_Arb {
    public:
    union cache_req_t *lsu_cache_req;
    union cache_req_t *ldq_cache_req;
    union cache_req_t *stq_cache_req;
    union cache_req_t *cache_req;

    public:
    void arbit_req_forepart();
    void arbit_req_backpart();
};