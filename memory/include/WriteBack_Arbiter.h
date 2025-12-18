#include "IO.h"
#include  <config.h>

class WriteBack_Arbiter_IN {
public:
    Mem_RESP* mshr_resp;
    Mem_RESP* dcache_st_resp;
    Mem_RESP* dcache_ld_resp;
};
class WriteBack_Arbiter_OUT {
public:
    Mem_RESP* ld_resp;
    Mem_RESP* st_resp;
    WB_Arbiter_Dcache wb_arbiter2dcache;
};
class WriteBack_Arbiter {
public:
    WriteBack_Arbiter_IN in;
    WriteBack_Arbiter_OUT out;

    void seq();
    void comb();
};