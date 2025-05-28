#include "MMU.h"

void MMU::req() {
    vtag_next = mmu_req->s.vtag_in;
    valid_next = mmu_req->s.valid_in;
}

void MMU::resp() {
    mmu_resp->m.excp_out = false;
    mmu_resp->m.miss_out = false;
    if (valid)
        mmu_resp->m.okay_out = true;
    else 
        mmu_resp->m.okay_out = false;
    mmu_resp->m.ptag_out = vtag;
}

void MMU::seq() {
    vtag = vtag_next;
    valid = valid_next;
}