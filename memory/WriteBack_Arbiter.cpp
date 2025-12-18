#include "WriteBack_Arbiter.h"
void WriteBack_Arbiter::comb()
{
    if(in.mshr_resp->valid && !in.mshr_resp->wen){
        out.ld_resp = in.mshr_resp;
        out.wb_arbiter2dcache.stall_ld = in.dcache_ld_resp->valid;
    }else{
        out.ld_resp = in.dcache_ld_resp;
        out.wb_arbiter2dcache.stall_ld = false;
    }

    if(in.mshr_resp->valid && in.mshr_resp->wen){
        out.st_resp = in.mshr_resp;
        out.wb_arbiter2dcache.stall_st = in.dcache_st_resp->valid;
    }
    else{
        out.st_resp = in.dcache_st_resp;
        out.wb_arbiter2dcache.stall_st = false;
    }
}
void WriteBack_Arbiter::seq()
{
    // empty
}