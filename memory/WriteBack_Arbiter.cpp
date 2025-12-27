#include "WriteBack_Arbiter.h"
void WriteBack_Arbiter::comb()
{
    if(in.mshr_resp->valid && !in.mshr_resp->wen){
        *out.ld_resp = *in.mshr_resp;
        out.wb_arbiter2dcache->stall_ld = in.dcache_ld_resp->valid;
    }else{
        *out.ld_resp = *in.dcache_ld_resp;
        out.wb_arbiter2dcache->stall_ld = false;
    }

    if(in.mshr_resp->valid && in.mshr_resp->wen){
        *out.st_resp = *in.mshr_resp;
        out.wb_arbiter2dcache->stall_st = in.dcache_st_resp->valid;
    }
    else{
        *out.st_resp = *in.dcache_st_resp;
        out.wb_arbiter2dcache->stall_st = false;
    }
    if(DCACHE_LOG){
        print();
    }
}
void WriteBack_Arbiter::seq()
{
    // empty
}
void WriteBack_Arbiter::print()
{
    printf("\nWriteBack Arbiter Input:\n");
    printf("  MSHR Resp: valid=%d, uop_inst=0x%08x rob_idx=%d wen=%d, addr=0x%08x, data=0x%08x \n",
           in.mshr_resp->valid, in.mshr_resp->uop.instruction, in.mshr_resp->uop.rob_idx, in.mshr_resp->wen, in.mshr_resp->addr, in.mshr_resp->data);
    printf("  Dcache LD Resp: valid=%d, uop_inst=0x%08x rob_idx=%d wen=%d, addr=0x%08x, data=0x%08x \n",
           in.dcache_ld_resp->valid, in.dcache_ld_resp->uop.instruction, in.dcache_ld_resp->uop.rob_idx, in.dcache_ld_resp->wen, in.dcache_ld_resp->addr, in.dcache_ld_resp->data);
    printf("  Dcache ST Resp: valid=%d, uop_inst=0x%08x rob_idx=%d wen=%d, addr=0x%08x, data=0x%08x \n",
           in.dcache_st_resp->valid, in.dcache_st_resp->uop.instruction, in.dcache_st_resp->uop.rob_idx, in.dcache_st_resp->wen, in.dcache_st_resp->addr, in.dcache_st_resp->data);
    printf("WriteBack Arbiter Output:\n");
    printf("  LD Resp: valid=%d, uop_inst=0x%08x, rob_idx=%d wen=%d, addr=0x%08x, data=0x%08x \n",
           out.ld_resp->valid, out.ld_resp->uop.instruction, out.ld_resp->uop.rob_idx, out.ld_resp->wen, out.ld_resp->addr, out.ld_resp->data);
    printf("  ST Resp: valid=%d, uop_inst=0x%08x, rob_idx=%d wen=%d, addr=0x%08x, data=0x%08x \n",
           out.st_resp->valid, out.st_resp->uop.instruction, out.st_resp->uop.rob_idx, out.st_resp->wen, out.st_resp->addr, out.st_resp->data);
    printf("  WB Arbiter to Dcache: stall_ld=%d, stall_st=%d\n",
           out.wb_arbiter2dcache->stall_ld, out.wb_arbiter2dcache->stall_st);
}