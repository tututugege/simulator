#include "CSR.h"
#include "IO.h"
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstring>
#include <cvt.h>
#include <diff.h>
#include <util.h>

void Back_Top::difftest_cycle()
{

  int commit_num = 0;
  Inst_uop *inst;
  bool skip = false;
  for (int i = 0; i < COMMIT_WIDTH; i++)
  {
    if (rob.out.rob_commit->commit_entry[i].valid)
    {
      commit_num++;
      inst = &rob.out.rob_commit->commit_entry[i].uop;
      if (inst->difftest_skip)
      {
        skip = true;
      }
    }
  }

  if (commit_num > 0)
  {
    for (int i = 0; i < commit_num - 1; i++)
    {
      difftest_step(false);
    }

    if (is_store(*inst))
    {
      if (stq.entry[inst->stq_idx].addr == 0x10000001 &&
          (stq.entry[inst->stq_idx].data & 0x000000ff) == 7)
      {
        csr.CSR_RegFile_1[csr_mip] = csr.CSR_RegFile[csr_mip] | (1 << 9);
        csr.CSR_RegFile_1[csr_sip] = csr.CSR_RegFile[csr_sip] | (1 << 9);
      }

      if (stq.entry[inst->stq_idx].addr == 0xc201004 &&
          (stq.entry[inst->stq_idx].data & 0x000000ff) == 0xa)
      {
        csr.CSR_RegFile_1[csr_mip] = csr.CSR_RegFile[csr_mip] & ~(1 << 9);
        csr.CSR_RegFile_1[csr_sip] = csr.CSR_RegFile[csr_sip] & ~(1 << 9);
      }
    }

    for (int i = 0; i < ARF_NUM; i++)
    {
      dut_cpu.gpr[i] = prf.reg_file[rename.arch_RAT_1[i]];
    }

    if (is_store(*inst))
    {
      dut_cpu.store = true;
      dut_cpu.store_addr = stq.entry[inst->stq_idx].addr;
      if (stq.entry[inst->stq_idx].size == 0b00)
        dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFF;
      else if (stq.entry[inst->stq_idx].size == 0b01)
        dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFFFF;
      else
        dut_cpu.store_data = stq.entry[inst->stq_idx].data;

      dut_cpu.store_data = dut_cpu.store_data
                           << (dut_cpu.store_addr & 0b11) * 8;
    }
    else
      dut_cpu.store = false;

    for (int i = 0; i < CSR_NUM; i++)
    {
      dut_cpu.csr[i] = csr.CSR_RegFile_1[i];
    }
    dut_cpu.pc = inst->pc_next;
    if (skip)
    {
      difftest_skip();
    }
    else
    {
      difftest_step(true);
    }
  }
}

void Back_Top::difftest_inst(Inst_uop *inst)
{

  if (inst->type == JALR)
  {
    if (inst->src1_areg == 1 && inst->dest_areg == 0 && inst->imm == 0)
    {
      perf.ret_br_num++;
    }
    else
    {
      perf.jalr_br_num++;
    }
  }
  else if (inst->type == BR)
  {
    perf.cond_br_num++;
  }

  if (inst->mispred)
  {
    if (inst->type == JALR)
    {
      if (inst->src1_areg == 1 && inst->dest_areg == 0 && inst->imm == 0)
      {
        perf.ret_mispred_num++;
        if (!inst->pred_br_taken)
        {
          perf.ret_dir_mispred++;
        }
        else
        {
          perf.ret_addr_mispred++;
        }
      }
      else
      {
        perf.jalr_mispred_num++;
        if (!inst->pred_br_taken)
        {
          perf.jalr_dir_mispred++;
        }
        else
        {
          perf.jalr_addr_mispred++;
        }
      }
    }
    else if (inst->type == BR)
    {
      if (inst->pred_br_taken != inst->br_taken)
      {
        perf.cond_dir_mispred++;
      }
      else
      {
        perf.cond_addr_mispred++;
      }
      perf.cond_mispred_num++;
    }
  }

  if (is_store(*inst))
  {
    if (stq.entry[inst->stq_idx].addr == 0x10000001 &&
        (stq.entry[inst->stq_idx].data & 0x000000ff) == 7)
    {
      csr.CSR_RegFile_1[csr_mip] = csr.CSR_RegFile[csr_mip] | (1 << 9);
      csr.CSR_RegFile_1[csr_sip] = csr.CSR_RegFile[csr_sip] | (1 << 9);
    }

    if (stq.entry[inst->stq_idx].addr == 0xc201004 &&
        (stq.entry[inst->stq_idx].data & 0x000000ff) == 0xa)
    {
      csr.CSR_RegFile_1[csr_mip] = csr.CSR_RegFile[csr_mip] & ~(1 << 9);
      csr.CSR_RegFile_1[csr_sip] = csr.CSR_RegFile[csr_sip] & ~(1 << 9);
    }
  }

#ifdef CONFIG_DIFFTEST
  if (LOG)
  {
    cout << "Instruction: " << inst->instruction << endl;
    if (inst->page_fault_inst)
      cout << "page fault inst " << endl;
    if (inst->page_fault_load)
      cout << "page fault load " << endl;
    if (inst->page_fault_store)
      cout << "page fault store " << endl;
  }

  for (int i = 0; i < ARF_NUM; i++)
  {
    dut_cpu.gpr[i] = prf.reg_file[rename.arch_RAT_1[i]];
  }

  if (is_store(*inst))
  {
    dut_cpu.store = true;
    dut_cpu.store_addr = stq.entry[inst->stq_idx].addr;
    if (stq.entry[inst->stq_idx].size == 0b00)
      dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFF;
    else if (stq.entry[inst->stq_idx].size == 0b01)
      dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFFFF;
    else
      dut_cpu.store_data = stq.entry[inst->stq_idx].data;

    dut_cpu.store_data = dut_cpu.store_data << (dut_cpu.store_addr & 0b11) * 8;
  }
  else
    dut_cpu.store = false;

  for (int i = 0; i < CSR_NUM; i++)
  {
    dut_cpu.csr[i] = csr.CSR_RegFile_1[i];
  }
  dut_cpu.pc = inst->pc_next;

  if (inst->difftest_skip)
  {
    difftest_skip();
  }
  else
  {
    difftest_step(true);
  }
#endif
}

Front_Dec front2dec;
Dec_Front dec2front;

Dec_Ren dec2ren;
Dec_Broadcast dec_bcast;

Ren_Dec ren2dec;
Ren_Dis ren2dis;

Dis_Ren dis2ren;
Dis_Iss dis2iss;
Dis_Rob dis2rob;
Dis_Stq dis2stq;

Iss_Awake iss_awake;
Iss_Prf iss2prf;
Iss_Dis iss2dis;

Prf_Exe prf2exe;
Prf_Rob prf2rob;
Prf_Awake prf_awake;
Prf_Dec prf2dec;

Exe_Prf exe2prf;
Exe_Stq exe2stq;
Exe_Iss exe2iss;

Rob_Dis rob2dis;
Rob_Csr rob2csr;
Rob_Broadcast rob_bcast;
Rob_Commit rob_commit;

Stq_Dis stq2dis;
Stq_Front stq2front;

Csr_Exe csr2exe;
Csr_Rob csr2rob;
Csr_Front csr2front;
Csr_Status csr_status;
Exe_Csr exe2csr;

#ifdef CONFIG_CACHE
Mem_REQ exu2lsu_req;
Mem_RESP lsu2stq_resp;
Mem_READY lsu2stq_ready;

Mem_REQ stq2lsu_req;
Mem_RESP lsu2prf_resp;
Mem_READY lsu2exe_ready;

Dcache_CONTROL exe2cache_control;
WB_Arbiter_Dcache wb_arbiter2dcache;
WriteBuffer_Dcache wb2dcache;

Mem_RESP dcache2stq_resp;
Mem_RESP dcache2prf_resp;

Dcache_MSHR dcache2mshr_ld;
Dcache_MSHR dcache2mshr_st;

EXMem_DATA arbiter2mshr_data;
WB_MSHR writebuffer2mshr;
Mem_RESP mshr2cpu_resp;
Mem_READY mshr2dcache_ready;
EXMem_CONTROL mshr2arbiter_control;

MSHR_WB mshr2writebuffer;
MSHR_FWD mshr2dcache_fwd;
MSHR_Arbiter mshr2arbiter;

EXMem_DATA arbiter2writebuffer_data;
EXMem_CONTROL writebuffer2arbiter_control;
WB_Arbiter writebuffer2arbiter;

EXMem_DATA mem_data;
EXMem_CONTROL mem_control;
#endif


void Back_Top::init()
{
  idu.out.dec2front = &dec2front;
  idu.out.dec2ren = &dec2ren;
  idu.out.dec_bcast = &dec_bcast;
  idu.in.front2dec = &front2dec;
  idu.in.ren2dec = &ren2dec;
  idu.in.prf2dec = &prf2dec;
  idu.in.rob_bcast = &rob_bcast;
  idu.in.commit = &rob_commit;

  rename.in.dec2ren = &dec2ren;
  rename.in.dis2ren = &dis2ren;
  rename.in.iss_awake = &iss_awake;
  rename.in.prf_awake = &prf_awake;
  rename.in.dec_bcast = &dec_bcast;
  rename.in.rob_bcast = &rob_bcast;
  rename.in.rob_commit = &rob_commit;

  rename.out.ren2dec = &ren2dec;
  rename.out.ren2dis = &ren2dis;

  dis.out.dis2ren = &dis2ren;
  dis.out.dis2iss = &dis2iss;
  dis.out.dis2rob = &dis2rob;
  dis.out.dis2stq = &dis2stq;

  dis.in.ren2dis = &ren2dis;
  dis.in.iss2dis = &iss2dis;
  dis.in.rob2dis = &rob2dis;
  dis.in.stq2dis = &stq2dis;
  dis.in.prf_awake = &prf_awake;
  dis.in.iss_awake = &iss_awake;
  dis.in.rob_bcast = &rob_bcast;
  dis.in.dec_bcast = &dec_bcast;

  isu.in.rob_bcast = &rob_bcast;
  isu.in.dec_bcast = &dec_bcast;
  isu.in.dis2iss = &dis2iss;
  isu.in.prf_awake = &prf_awake;
  isu.in.exe2iss = &exe2iss;

  isu.out.iss2dis = &iss2dis;
  isu.out.iss2prf = &iss2prf;
  isu.out.iss_awake = &iss_awake;

  prf.out.prf2rob = &prf2rob;
  prf.out.prf_awake = &prf_awake;
  prf.out.prf2dec = &prf2dec;
  prf.out.prf2exe = &prf2exe;

  prf.in.iss2prf = &iss2prf;
  prf.in.exe2prf = &exe2prf;
  prf.in.dec_bcast = &dec_bcast;
  prf.in.rob_bcast = &rob_bcast;

  exu.in.prf2exe = &prf2exe;
  exu.in.dec_bcast = &dec_bcast;
  exu.in.rob_bcast = &rob_bcast;
  exu.out.exe2prf = &exe2prf;
  exu.out.exe2stq = &exe2stq;
  exu.out.exe2iss = &exe2iss;
  exu.out.exe2csr = &exe2csr;
  exu.in.csr2exe = &csr2exe;

  rob.in.dis2rob = &dis2rob;
  rob.in.dec_bcast = &dec_bcast;
  rob.in.prf2rob = &prf2rob;
  rob.in.dec_bcast = &dec_bcast;
  rob.in.csr2rob = &csr2rob;

  rob.out.rob_bcast = &rob_bcast;
  rob.out.rob_commit = &rob_commit;
  rob.out.rob2dis = &rob2dis;
  rob.out.rob2csr = &rob2csr;

  stq.out.stq2dis = &stq2dis;
  stq.out.stq2front = &stq2front;

  stq.in.exe2stq = &exe2stq;
  stq.in.rob_commit = &rob_commit;
  stq.in.dis2stq = &dis2stq;
  stq.in.dec_bcast = &dec_bcast;
  stq.in.rob_bcast = &rob_bcast;

  csr.in.exe2csr = &exe2csr;
  csr.in.rob2csr = &rob2csr;
  csr.in.rob_bcast = &rob_bcast;

  csr.out.csr2exe = &csr2exe;
  csr.out.csr2rob = &csr2rob;
  csr.out.csr2front = &csr2front;
  csr.out.csr_status = &csr_status;

#ifdef CONFIG_CACHE
  exu.out.exe2cache_control = &exe2cache_control;

  stq.in.cache2stq = &lsu2stq_resp;
  stq.in.cache2stq_ready = &lsu2stq_ready;
  stq.out.stq2cache_req = &stq2lsu_req;

  exu.out.exe2cache = &exu2lsu_req;
  exu.in.cache2exe_ready = &lsu2exe_ready;
  prf.in.cache2prf = &lsu2prf_resp;

  dcache.in.ldq2dcache_req = &exu2lsu_req;
  dcache.in.stq2dcache_req = &stq2lsu_req;
  dcache.in.control = &exe2cache_control;
  dcache.in.wb2dcache = &wb2dcache;
  dcache.in.wb_arbiter2dcache = &wb_arbiter2dcache;
  dcache.in.mshr2dcache_ready = &mshr2dcache_ready;
  dcache.in.mshr2dcache_fwd = &mshr2dcache_fwd;

  dcache.out.dcache2ldq_ready = &lsu2exe_ready;
  dcache.out.dcache2ldq_resp = &dcache2prf_resp;
  dcache.out.dcache2stq_resp = &dcache2stq_resp;
  dcache.out.dcache2stq_ready = &lsu2stq_ready;
  dcache.out.dcache2mshr_ld = &dcache2mshr_ld;
  dcache.out.dcache2mshr_st = &dcache2mshr_st;

  mshr.in.dcache2mshr_ld = &dcache2mshr_ld;
  mshr.in.dcache2mshr_st = &dcache2mshr_st;
  mshr.in.control = &exe2cache_control;
  mshr.in.arbiter2mshr_data = &arbiter2mshr_data;
  mshr.in.writebuffer2mshr = &writebuffer2mshr;

  mshr.out.mshr2cpu_resp = &mshr2cpu_resp;
  mshr.out.mshr2dcache_ready = &mshr2dcache_ready;
  mshr.out.mshr2arbiter_control = &mshr2arbiter_control;
  mshr.out.mshr2writebuffer = &mshr2writebuffer;
  mshr.out.mshr2dcache_fwd = &mshr2dcache_fwd;
  mshr.out.mshr2arbiter = &mshr2arbiter;

  writebuffer.in.mshr2writebuffer = &mshr2writebuffer;
  writebuffer.in.arbiter2writebuffer_data = &arbiter2writebuffer_data;

  writebuffer.out.writebuffer2arbiter = &writebuffer2arbiter;
  writebuffer.out.writebuffer2arbiter_control = &writebuffer2arbiter_control;
  writebuffer.out.writebuffer2mshr = &writebuffer2mshr;

  arbiter.in.writebuffer2arbiter = &writebuffer2arbiter;
  arbiter.in.writebuffer2arbiter_control = &writebuffer2arbiter_control;
  arbiter.in.mshr2arbiter_control = &mshr2arbiter_control;
  arbiter.in.mshr2arbiter = &mshr2arbiter;
  arbiter.in.mem_data = &mem_data;

  arbiter.out.arbiter2mshr_data = &arbiter2mshr_data;
  arbiter.out.arbiter2writebuffer_data = &arbiter2writebuffer_data;
  arbiter.out.mem_control = &mem_control;

  memory.in.control = &mem_control;
  memory.out.data = &mem_data;

  wb_arbiter.in.dcache_ld_resp = &dcache2prf_resp;
  wb_arbiter.in.dcache_st_resp = &dcache2stq_resp;
  wb_arbiter.in.mshr_resp = &mshr2cpu_resp;
  wb_arbiter.out.wb_arbiter2dcache = &wb_arbiter2dcache;
  wb_arbiter.out.ld_resp = &lsu2prf_resp;
  wb_arbiter.out.st_resp = &lsu2stq_resp;

#if defined(CONFIG_MMU)
  dcache.out.dcache2ptw_resp = &out.dcache2ptw_resp;
  dcache.out.dcache2ptw_req = &out.dcache2ptw_req;
  dcache.in.ptw2dcache_req = &in.ptw2dcache_req;
  dcache.in.ptw2dcache_resp = &in.ptw2dcache_resp;
#endif
#endif

  idu.init();
  isu.init();
  prf.init();
  exu.init();
  csr.init();
  rob.init();

#ifdef CONFIG_CACHE
  dcache.init();
  mshr.init();
  writebuffer.init();
  arbiter.init();
  memory.init();
#endif
}

void Back_Top::comb_csr_status()
{
  csr.comb_csr_status();
  out.sstatus = csr.out.csr_status->sstatus;
  out.mstatus = csr.out.csr_status->mstatus;
  out.satp = csr.out.csr_status->satp;
  out.privilege = csr.out.csr_status->privilege;
}

void Back_Top::comb()
{
  // 输出提交的指令
  for (int i = 0; i < FETCH_WIDTH; i++)
  {
    idu.in.front2dec->valid[i] = in.valid[i];
    idu.in.front2dec->pc[i] = in.pc[i];
    idu.in.front2dec->inst[i] = in.inst[i];
    idu.in.front2dec->predict_dir[i] = in.predict_dir[i];
    idu.in.front2dec->alt_pred[i] = in.alt_pred[i];
    idu.in.front2dec->altpcpn[i] = in.altpcpn[i];
    idu.in.front2dec->pcpn[i] = in.pcpn[i];
    idu.in.front2dec->predict_next_fetch_address[i] =
        in.predict_next_fetch_address[i];
    idu.in.front2dec->page_fault_inst[i] = in.page_fault_inst[i];
  }

  // 每个空行表示分层  下层会依赖上层产生的某个信号
  idu.comb_decode();
  prf.comb_br_check();
  csr.comb_interrupt();
  rename.comb_alloc();
  prf.comb_complete();
  prf.comb_awake();
  prf.comb_write();
  isu.comb_ready();

  idu.comb_branch();
  rob.comb_complete();

  rob.comb_ready();
  rob.comb_commit();

  idu.comb_release_tag();
  dis.comb_alloc();

  exu.comb_exec();

#ifdef CONFIG_CACHE
#endif
  stq.comb_out();
  mshr.comb_ready();
  writebuffer.comb_ready();
  writebuffer.comb_writemark();
  dcache.comb_s2();
  writebuffer.comb();
  arbiter.comb_in();
  memory.comb();
  arbiter.comb_out();
  mshr.comb_out();
  dcache.comb_out_ldq();
  wb_arbiter.comb();
  prf.comb_load();
  dcache.comb_s1();
  dcache.comb_out_mshr();
  dcache.comb_out_ready();
  mshr.comb();
  stq.comb_in();
  exu.comb_latency();
#ifdef CONFIG_MMU
  dcache.comb_mmu();
#else

  stq.comb();
#endif

  exu.comb_to_csr();
  exu.comb_ready();
  isu.comb_deq();

  isu.comb_awake();
  csr.comb_exception();
  csr.comb_csr_read();
  csr.comb_csr_write();
  exu.comb_from_csr();
  prf.comb_read();
  rename.comb_wake();
  dis.comb_wake();
  rename.comb_rename();

  dis.comb_dispatch();

  dis.comb_fire();
  rename.comb_fire();
  rob.comb_fire();
  idu.comb_fire();

  // 为了debug
  // 修正pc_next 以及difftest对应的pc_next
  back.out.flush = rob.out.rob_bcast->flush;
  if (!rob.out.rob_bcast->flush)
  {
    back.out.mispred = prf.out.prf2dec->mispred;
    back.out.stall = !idu.out.dec2front->ready;
    back.out.redirect_pc = prf.out.prf2dec->redirect_pc;
  }
  else
  {
    if (LOG)
      cout << "flush" << endl;
    back.out.mispred = true;
    if (rob.out.rob_bcast->mret || rob.out.rob_bcast->sret)
    {
      back.out.redirect_pc = csr.out.csr2front->epc;
    }
    else if (rob.out.rob_bcast->exception)
    {
      back.out.redirect_pc = csr.out.csr2front->trap_pc;
    }
    else
    {
      back.out.redirect_pc = rob.out.rob_bcast->pc + 4;
    }
  }

  for (int i = 0; i < COMMIT_WIDTH; i++)
  {
    back.out.commit_entry[i] = rob.out.rob_commit->commit_entry[i];
    Inst_type type = back.out.commit_entry[i].uop.type;
    if (back.out.commit_entry[i].valid && back.out.flush)
    {
      back.out.commit_entry[i].uop.pc_next = back.out.redirect_pc;
      rob.out.rob_commit->commit_entry[i].uop.pc_next = back.out.redirect_pc;
    }
  }

  isu.comb_enq();
  rename.comb_commit(); // difftest在这里
  rob.comb_flush();
  prf.comb_flush();
  exu.comb_flush();
  rename.comb_flush();
  idu.comb_flush();
  isu.comb_flush();
  rob.comb_branch();
  prf.comb_branch();
  exu.comb_branch();
  rename.comb_branch();
  isu.comb_branch();
  prf.comb_pipeline();
  exu.comb_pipeline();
  dis.comb_pipeline();
  rename.comb_pipeline();
}

void Back_Top::seq()
{
  // rename -> isu/stq/rob
  // exu -> prf
  rename.seq();
  dis.seq();
  idu.seq();
  isu.seq();
  exu.seq();
#ifdef CONFIG_CACHE
  dcache.seq();
  mshr.seq();
  writebuffer.seq();
  arbiter.seq();
  memory.seq();
  wb_arbiter.seq();
#endif
  prf.seq();
  rob.seq();
  stq.seq();
  csr.seq();
  for (int i = 0; i < FETCH_WIDTH; i++)
  {
    out.fire[i] = idu.out.dec2front->fire[i];
  }
}

#ifdef CONFIG_CACHE
#elif CONFIG_MMU
bool Back_Top::load_data(uint32_t &data, uint32_t v_addr, int rob_idx,
                         bool &mmu_page_fault, uint32_t &mmu_ppn,
                         bool &stall_load)
{
  uint32_t p_addr = v_addr;
  bool ret = true;

  p_addr = mmu_ppn << 12 | (v_addr & 0xFFF);
  ret = !mmu_page_fault;

  if (p_addr == 0x1fd0e000)
  {
    data = perf.commit_num;
  }
  else if (p_addr == 0x1fd0e004)
  {
    data = 0;
  }
  else
  {
    data = p_memory[p_addr >> 2];
    back.stq.st2ld_fwd(p_addr, data, rob_idx, stall_load);
  }

  return ret;
}
#else
bool Back_Top::load_data(uint32_t &data, uint32_t v_addr, int rob_idx)
{
  uint32_t p_addr = v_addr;
  bool ret = true;

  if (back.out.satp & 0x80000000 && back.out.privilege != 3)
  {
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, back.out.mstatus, 32);

    cvt_number_to_bit_unsigned(sstatus, back.out.sstatus, 32);

    ret = va2pa(p_addr, v_addr, back.out.satp, 1, mstatus, sstatus,
                back.out.privilege, p_memory);
  }

  if (p_addr == 0x1fd0e000)
  {
    data = perf.commit_num;
  }
  else if (p_addr == 0x1fd0e004)
  {
    data = 0;
  }
  else
  {
    data = p_memory[p_addr >> 2];
    bool stall = false;
    back.stq.st2ld_fwd(p_addr, data, rob_idx, stall);
  }

  return ret;
}
#endif
