#include <Cache.h>
#include <EXU.h>
#include <MMU.h>
#include <cmath>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <util.h>
extern MMU mmu;
extern uint32_t *p_memory;
Cache cache;

bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory, bool dut_flag = true);
void alu(Inst_uop &inst);
void bru(Inst_uop &inst);
void stu_data(Inst_uop &inst);
void mul(Inst_uop &inst);
void div(Inst_uop &inst);

#ifdef CONFIG_CACHE
#ifdef CONFIG_MMU
void ldu(Inst_uop &inst, bool mmu_page_fault, uint32_t mmu_ppn, Mem_REQ *&in);
void stu_addr(Inst_uop &inst, bool page_fault_mmu, uint32_t mmu_ppn);
#else
void ldu(Inst_uop &inst, Mem_REQ *&in);
void stu_addr(Inst_uop &inst);
#endif
#elif CONFIG_MMU
bool ldu(Inst_uop &inst, bool mmu_page_fault, uint32_t mmu_ppn);
void stu_addr(Inst_uop &inst, bool page_fault_mmu, uint32_t mmu_ppn);
#else
bool ldu(Inst_uop &inst);
void stu_addr(Inst_uop &inst);
#endif

bool ldu_work = false;
mmu_resp_master_t resp;
bool page_fault;
uint32_t mmu_ppn;
// Calculate the state of mmu_lsu_req[M] channel
wire1_t alloc_mmu_req_slot[MAX_LSU_REQ_NUM];

// 请求 LSU 槽位 (grant or not)
static inline bool comb_apply_slot(mmu_slot_t &slot)
{
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++)
  {
    if (!alloc_mmu_req_slot[i])
    {
      slot.idx = i;
      slot.valid = true;
      alloc_mmu_req_slot[i] = true;
      return true;
    }
  }
  slot.valid = false;
  slot.idx = 0;
  return false;
}
#if defined(CONFIG_CACHE) && !defined(CONFIG_MMU)
void FU::exec(Inst_uop &inst, Mem_REQ *&in, bool mispred)
{

  if (cycle == 0)
  {
    if (inst.op == UOP_MUL)
    { // mul
      latency = 1;
    }
    else if (inst.op == UOP_DIV)
    { // div
      latency = 1;
    }
    else if (inst.op == UOP_LOAD)
    {
      latency = 1;
      cache.cache_access(inst.src1_rdata + inst.imm);
      // latency = 2;
    }
    else if (inst.op == UOP_STA)
    {
      latency = 2;
    }
    else
    {
      latency = 1;
    }
  }

  cycle++;
  if (mispred)
  {
    complete = true;
    cycle = 0;
    return;
  }
  if (cycle == latency)
  {
    if (is_load_uop(inst.op))
    {
      ldu(inst, in);
    }
    else if (is_sta_uop(inst.op))
    {
      stu_addr(inst);
    }
    else if (is_std_uop(inst.op))
    {
      stu_data(inst);
    }
    else if (is_branch_uop(inst.op))
    {
      bru(inst);
    }
    else if (inst.op == UOP_MUL)
    {
      mul(inst);
    }
    else if (inst.op == UOP_DIV)
    {
      div(inst);
    }
    else if (inst.op == UOP_SFENCE_VMA)
    {
      uint32_t vaddr = 0;
      uint32_t asid = 0;
      // TODO: sfence.vma
    }
    else
      alu(inst);

    if (!is_load_uop(inst.op))
    {
      complete = true;
      cycle = 0;
    }
  }
}

#elif defined(CONFIG_CACHE) && defined(CONFIG_MMU)
void FU::exec(Inst_uop &inst, Mem_REQ *&in, bool mispred)
{

  if (cycle == 0)
  {
    if (inst.op == UOP_MUL)
    { // mul
      latency = 1;
    }
    else if (inst.op == UOP_DIV)
    { // div
      latency = 1;
    }
    else if (inst.op == UOP_LOAD)
    {
      cache.cache_access(inst.src1_rdata + inst.imm);
      latency = 1;
    }
    else if (inst.op == UOP_STA)
    {
      latency = 2;
    }
    else
    {
      latency = 1;
    }
  }

  cycle++;
  if (DCACHE_LOG)
  {
    printf("  FU exec: inst=0x%08x cycle=%d latency=%d mispred=%d uop=%d\n",
           inst.instruction, cycle, latency, mispred, inst.op);
    for (int i = 0; i < MAX_LSU_REQ_NUM; i++)
    {
      printf("    alloc_mmu_req_slot[%d]=%d\n", i, alloc_mmu_req_slot[i]);
    }
  }
  if (ldu_work == false|| is_sta_uop(inst.op))
  {
    // deal with mmu: if miss, return and wait for next cycle
    if (is_load_uop(inst.op) || is_sta_uop(inst.op))
    {
      // step0: reset wire mmu_lsu_slot_r_1, which is
      // only useful for load/sta uop
      uint32_t vaddr = inst.src1_rdata + inst.imm;
      // step1: try to find a free slot if not allocated yet
      if (!mmu_lsu_slot_r.valid)
      {
        // slot not allocated yet
        bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
        if (granted)
        {
          // free slot found, send mmu request and
          // waiting for next cycle
          mmu_req_master_t req = {
              .valid = true,
              .vtag = (vaddr >> 12), // vaddr[31:12]
              .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
          int idx = mmu_lsu_slot_r_1.idx;
          mmu.io.in.mmu_lsu_req[idx] = req;
        }
        if (DCACHE_LOG)
        {
          printf("   FU exec: mmu slot not allocated, granted=%d\n", granted);
        }
        return;
      }

      // step2: slot aleardy allocated, check mmu resp to see if hit
      mmu_resp_master_t resp = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool hit = resp.valid && !resp.miss;
      // if (!hit) {
      if (!hit || cycle < latency)
      {
        // miss, reallocate and replay the request
        bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
        if (granted)
        {
          mmu_req_master_t req = {
              .valid = true,
              .vtag = (vaddr >> 12), // vaddr[31:12]
              .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
          mmu.io.in.mmu_lsu_req[mmu_lsu_slot_r_1.idx] = req;
        }
        if(DCACHE_LOG){
          printf("   FU exec: mmu miss, reallocate slot, granted=%d hit=%d\n", granted, hit);
        }
        return;
      }
    }
  }

  if (mispred)
  {
    complete = true;
    cycle = 0;
    return;
  }
  if (DCACHE_LOG)
  {
    printf("cycle=%d latency=%d inst=0x%08x rob_idx=%d ldu_work=%d\n", cycle, latency, inst.instruction, inst.rob_idx, ldu_work);
  }
  if (cycle >= latency)
  {
    if (is_load_uop(inst.op))
    {
      if(!ldu_work)
      {
        resp = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
        page_fault = resp.valid && resp.excp;
        mmu_ppn = resp.ptag;
      }
      // ldu(inst);
      if(DCACHE_LOG){
        printf("  FU exec: before ldu, page_fault=%d mmu_ppn=0x%08x inst=0x%08x rob_idx=%d\n", page_fault, mmu_ppn, inst.instruction, inst.rob_idx);
      }
      ldu_work = true;
      ldu(inst, page_fault, mmu_ppn, in);
    }
    else if (is_sta_uop(inst.op))
    {
      mmu_resp_master_t resp_st = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool page_fault_st = resp_st.valid && resp_st.excp;
      uint32_t mmu_ppn_st = resp_st.ptag;
      if(DCACHE_LOG){
        printf("  FU exec: before stu_addr, page_fault=%d mmu_ppn=0x%08x inst=0x%08x rob_idx=%d\n", page_fault_st, mmu_ppn_st, inst.instruction, inst.rob_idx);
      }
      stu_addr(inst, page_fault_st, mmu_ppn_st);
    }
    else if (is_std_uop(inst.op))
    {
      stu_data(inst);
    }
    else if (is_branch_uop(inst.op))
    {
      bru(inst);
    }
    else if (inst.op == UOP_MUL)
    {
      mul(inst);
    }
    else if (inst.op == UOP_DIV)
    {
      div(inst);
    }
    else if (inst.op == UOP_SFENCE_VMA)
    {
      uint32_t vaddr = inst.src1_rdata;
      uint32_t asid = inst.src2_rdata;
      mmu.io.in.tlb_flush.flush_asid = asid;
      mmu.io.in.tlb_flush.flush_vpn = vaddr >> 12;
      mmu.io.in.tlb_flush.flush_valid = true;


      static int sfence_count = 0;
      sfence_count++;
      // if(DCACHE_LOG){
        printf("  FU exec: sfence.vma #%d vaddr=0x%08x asid=0x%08x sim_time=%lld\n", sfence_count, vaddr, asid,sim_time);
      // }
    }
    else
      alu(inst);

    if (!is_load_uop(inst.op))
    {
      complete = true;
      cycle = 0;
    }
  }
}
#elif !defined(CONFIG_CACHE) && defined(CONFIG_MMU)
void FU::exec(Inst_uop &inst)
{

  if (cycle == 0)
  {
    if (inst.op == UOP_MUL)
    { // mul
      latency = 1;
    }
    else if (inst.op == UOP_DIV)
    { // div
      latency = 1;
    }
    else if (inst.op == UOP_LOAD)
    {
      latency = cache.cache_access(inst.src1_rdata + inst.imm);
      // latency = 1;
    }
    else if (inst.op == UOP_STA)
    {
      latency = 2;
    }
    else
    {
      latency = 1;
    }
  }

  cycle++;

  // deal with mmu: if miss, return and wait for next cycle
  if (is_load_uop(inst.op) || is_sta_uop(inst.op))
  {
    // step0: reset wire mmu_lsu_slot_r_1, which is
    // only useful for load/sta uop
    uint32_t vaddr = inst.src1_rdata + inst.imm;
    // step1: try to find a free slot if not allocated yet
    if (!mmu_lsu_slot_r.valid)
    {
      // slot not allocated yet
      bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
      if (granted)
      {
        // free slot found, send mmu request and
        // waiting for next cycle
        mmu_req_master_t req = {
            .valid = true,
            .vtag = (vaddr >> 12), // vaddr[31:12]
            .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
        int idx = mmu_lsu_slot_r_1.idx;
        cpu.mmu.io.in.mmu_lsu_req[idx] = req;
      }
      return;
    }

    // step2: slot aleardy allocated, check mmu resp to see if hit
    mmu_resp_master_t resp = cpu.mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
    bool hit = resp.valid && !resp.miss;
    // if (!hit) {
    if (!hit || cycle < latency)
    {
      // miss, reallocate and replay the request
      bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
      if (granted)
      {
        mmu_req_master_t req = {
            .valid = true,
            .vtag = (vaddr >> 12), // vaddr[31:12]
            .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
        cpu.mmu.io.in.mmu_lsu_req[mmu_lsu_slot_r_1.idx] = req;
      }
      return;
    }
  }

  if (cycle >= latency)
  {
    if (is_load_uop(inst.op))
    {
      mmu_resp_master_t resp = cpu.mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool page_fault = resp.valid && resp.excp;
      uint32_t mmu_ppn = resp.ptag;
      // ldu(inst);
      bool stall_load = ldu(inst, page_fault, mmu_ppn);
      if (stall_load)
      {
        // load failed due to waiting forward data from
        // store queue reallocate and replay the request
        bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
        if (granted)
        {
          uint32_t vaddr = inst.src1_rdata + inst.imm;
          mmu_req_master_t req = {.valid = true,
                                  .vtag = (vaddr >> 12), // vaddr[31:12]
                                  .op_type = mmu_n::OP_LOAD};
        }
        return; // not complete yet
      }
    }
    else if (is_sta_uop(inst.op))
    {
      mmu_resp_master_t resp = cpu.mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool page_fault = resp.valid && resp.excp;
      uint32_t mmu_ppn = resp.ptag;
      stu_addr(inst, page_fault, mmu_ppn);
    }
    else if (is_std_uop(inst.op))
    {
      stu_data(inst);
    }
    else if (is_branch_uop(inst.op))
    {
      bru(inst);
    }
    else if (inst.op == UOP_MUL)
    {
      mul(inst);
    }
    else if (inst.op == UOP_DIV)
    {
      div(inst);
    }
    else if (inst.op == UOP_SFENCE_VMA)
    {
      uint32_t vaddr = inst.src1_rdata;
      uint32_t asid = inst.src2_rdata;
      cpu.mmu.io.in.tlb_flush.flush_asid = asid;
      cpu.mmu.io.in.tlb_flush.flush_vpn = vaddr >> 12;
      cpu.mmu.io.in.tlb_flush.flush_valid = true;
    }
    else
      alu(inst);

    complete = true;
    cycle = 0;
  }
}

#else
void FU::exec(Inst_uop &inst)
{

  if (cycle == 0)
  {
    if (inst.op == UOP_MUL)
    { // mul
      latency = 1;
    }
    else if (inst.op == UOP_DIV)
    { // div
      latency = 1;
    }
    else if (inst.op == UOP_LOAD)
    {
      latency = cache.cache_access(inst.src1_rdata + inst.imm);
      // latency = 1;
    }
    else if (inst.op == UOP_STA)
    {
      latency = 1;
    }
    else
    {
      latency = 1;
    }
  }

  cycle++;

  if (cycle == latency)
  {
    if (is_load_uop(inst.op))
    {
      ldu(inst);
    }
    else if (is_sta_uop(inst.op))
    {
      stu_addr(inst);
    }
    else if (is_std_uop(inst.op))
    {
      stu_data(inst);
    }
    else if (is_branch_uop(inst.op))
    {
      bru(inst);
    }
    else if (inst.op == UOP_MUL)
    {
      mul(inst);
    }
    else if (inst.op == UOP_DIV)
    {
      div(inst);
    }
    else if (inst.op == UOP_SFENCE_VMA)
    {
      uint32_t vaddr = 0;
      uint32_t asid = 0;
      // TODO: sfence.vma
    }
    else
      alu(inst);

    complete = true;
    cycle = 0;
  }
}
#endif

void EXU::init()
{
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    inst_r[i].valid = false;
  }
}

void EXU::comb_ready()
{
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    out.exe2iss->ready[i] = (!inst_r[i].valid || fu[i].complete) &&
                            !in.dec_bcast->mispred && !in.rob_bcast->flush;
  }
}

void EXU::comb_exec()
{
  // reset alloc_mmu_req_slot

#ifdef CONFIG_MMU
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++)
  {
    alloc_mmu_req_slot[i] = 0;
    mmu.io.in.mmu_lsu_req[i] = {}; // clear lsu req by default
  }
#endif

#ifdef CONFIG_CACHE
  out.exe2cache_control->flush = in.rob_bcast->flush;
  out.exe2cache_control->mispred = in.dec_bcast->mispred;
  out.exe2cache_control->br_mask = in.dec_bcast->br_mask;
  out.exe2cache->en = false;
#endif

  // comb_exec
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    fu[i].mmu_lsu_slot_r_1 = {};
    out.exe2prf->entry[i].valid = false;
    out.exe2prf->entry[i].uop = inst_r[i].uop;
    if (inst_r[i].valid)
    {
#ifdef CONFIG_CACHE
      fu[i].exec(out.exe2prf->entry[i].uop, out.exe2cache, (in.dec_bcast->mispred && ((1 << inst_r[i].uop.tag) & in.dec_bcast->br_mask)) || in.rob_bcast->flush);

      if (DCACHE_LOG)
      {
        printf("EXU FU[%d] Exec Inst: 0x%08x rob_idx=%d valid:%d op:%d ldu_work=%d\n", i, inst_r[i].uop.instruction, inst_r[i].uop.rob_idx, inst_r[i].valid, inst_r[i].uop.op, ldu_work);
      }
      if (i == IQ_LD)
        continue;
#else
      fu[i].exec(out.exe2prf->entry[i].uop);
#endif
      if (fu[i].complete &&
          !(in.dec_bcast->mispred &&
            ((1 << inst_r[i].uop.tag) & in.dec_bcast->br_mask)) &&
          !in.rob_bcast->flush)
      {
        out.exe2prf->entry[i].valid = true;
      }
      else
      {
        out.exe2prf->entry[i].valid = false;
      }
    }
  }

  // store
  if (inst_r[IQ_STA].valid)
  {
    out.exe2stq->addr_entry = out.exe2prf->entry[IQ_STA];
  }
  else
  {
    out.exe2stq->addr_entry.valid = false;
  }

  if (inst_r[IQ_STD].valid)
  {
    out.exe2stq->data_entry = out.exe2prf->entry[IQ_STD];
  }
  else
  {
    out.exe2stq->data_entry.valid = false;
  }
#ifdef CONFIG_CACHE
  out.exe2cache_control->flush = in.rob_bcast->flush;
  out.exe2cache_control->mispred = in.dec_bcast->mispred;
  out.exe2cache_control->br_mask = in.dec_bcast->br_mask;
#endif
}

void EXU::comb_to_csr()
{
  out.exe2csr->we = false;
  out.exe2csr->re = false;

  if (inst_r[0].valid && inst_r[0].uop.op == UOP_CSR && !in.rob_bcast->flush)
  {
    out.exe2csr->we = inst_r[0].uop.func3 == 1 || inst_r[0].uop.src1_areg != 0;
    out.exe2csr->re = inst_r[0].uop.func3 != 1 || inst_r[0].uop.dest_areg != 0;

    out.exe2csr->idx = inst_r[0].uop.csr_idx;
    out.exe2csr->wcmd = inst_r[0].uop.func3 & 0b11;
    if (inst_r[0].uop.src2_is_imm)
    {
      out.exe2csr->wdata = inst_r[0].uop.imm;
    }
    else
    {
      out.exe2csr->wdata = inst_r[0].uop.src1_rdata;
    }
  }
}

void EXU::comb_from_csr()
{
  if (inst_r[0].valid && inst_r[0].uop.op == UOP_CSR && out.exe2csr->re)
  {
    out.exe2prf->entry[0].uop.result = in.csr2exe->rdata;
  }
}

void EXU::comb_pipeline()
{
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    if (in.prf2exe->iss_entry[i].valid && out.exe2iss->ready[i])
    {
      inst_r_1[i] = in.prf2exe->iss_entry[i];
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
    else if (out.exe2prf->entry[i].valid && in.prf2exe->ready[i])
    {
      inst_r_1[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}

void EXU::comb_branch()
{
  if (in.dec_bcast->mispred)
  {
    for (int i = 0; i < ISSUE_WAY; i++)
    {
      if (inst_r[i].valid &&
          (in.dec_bcast->br_mask & (1 << inst_r[i].uop.tag)))
      {
        inst_r_1[i].valid = false;
        fu[i].complete = false;
        fu[i].cycle = 0;
      }
    }
  }
}

void EXU::comb_flush()
{
  if (in.rob_bcast->flush)
  {
    for (int i = 0; i < ISSUE_WAY; i++)
    {
      inst_r_1[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}

void EXU::seq()
{

  for (int i = 0; i < ISSUE_WAY; i++)
  {
    inst_r[i] = inst_r_1[i];

#ifdef CONFIG_MMU
    fu[i].mmu_lsu_slot_r = fu[i].mmu_lsu_slot_r_1;
#endif
  }
}
void EXU::comb_latency()
{
  // if(DCACHE_LOG){
  //   printf("\nEXU Latency Comb:in.cache2exe_ready->ready=%d\n", in.cache2exe_ready->ready);
  //   for(int i = 0; i < ISSUE_WAY; i++){
  //     printf("  FU[%d]: cycle=%d, latency=%d, complete=%d\n", i, fu[i].cycle, fu[i].latency, fu[i].complete);
  //   }
  //   printf("\n");
  //   for(int i = 0; i < ISSUE_WAY; i++)
  //   printf("  inst_r[%d]: valid=%d uop_inst=0x%08x\n", i, inst_r[i].valid, inst_r[i].uop.instruction);
  //   printf("\nmispred: %d flush: %d\n", in.dec_bcast->mispred, in.rob_bcast->flush);
  // }
  if (DCACHE_LOG)
  {
    printf("\nEXU Latency Comb:\n");
    printf("in.cache2exe_ready->ready: %d\n", in.cache2exe_ready->ready);
    printf("ldu_work: %d\n", ldu_work);
  }
  if (inst_r[IQ_LD].valid && ldu_work)
  {
    if (in.rob_bcast->flush)
    {
      fu[IQ_LD].complete = true;
      fu[IQ_LD].cycle = 0;
      out.exe2prf->entry[IQ_LD].valid = false;
      ldu_work = false;
      if (DCACHE_LOG)
      {
        printf("EXU Latency Comb: LD flush!\n");
      }
      return;
    }
    if (in.dec_bcast->mispred && ((1 << inst_r[IQ_LD].uop.tag) & in.dec_bcast->br_mask))
    {
      fu[IQ_LD].complete = true;
      fu[IQ_LD].cycle = 0;
      out.exe2prf->entry[IQ_LD].valid = false;
      ldu_work = false;
      if (DCACHE_LOG)
      {
        printf("EXU Latency Comb: LD mispred!\n");
      }
      return;
    }
    if (in.cache2exe_ready->ready)
    {
      fu[IQ_LD].complete = true;
      fu[IQ_LD].cycle = 0;
      out.exe2prf->entry[IQ_LD].valid = true;
      ldu_work = false;
      if (DCACHE_LOG)
      {
        printf("EXU Latency Comb: LD complete!\n");
      }
    }
    else
    {
      fu[IQ_LD].complete = false;
      fu[IQ_LD].latency++;
      out.exe2prf->entry[IQ_LD].valid = false;

      if (DCACHE_LOG)
      {
        printf("EXU Latency Comb: LD waiting...\n");
      }
    }
    // if (DCACHE_LOG)
    // {
    //   printf("\nEXU Latency Comb:\n");
    //   printf("in.cache2exe_ready->ready: %d fu[%d].complete: %d\n", in.cache2exe_ready->ready, IQ_LD, fu[IQ_LD].complete);
    // }
  }
}