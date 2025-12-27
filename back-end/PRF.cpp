#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <cstring>
#include <iostream>
#include <util.h>
#include "TOP.h"

bool stall_dcache = false;
void PRF::init()
{
  for (int i = 0; i < ISSUE_WAY; i++)
    out.prf2exe->ready[i] = true;
}

void PRF::comb_br_check()
{
  // 根据分支结果向前端返回信息
  bool mispred = false;
  Inst_uop *mispred_uop;

  for (int i = 0; i < BRU_NUM; i++)
  {
    int iq_br = IQ_BR0 + i;
    if (inst_r[iq_br].valid && inst_r[iq_br].uop.mispred)
    {
      if (LOG)
      {
        cout << hex << inst_r[iq_br].uop.pc << endl;
        cout << hex << inst_r[iq_br].uop.pc_next << endl;
        cout << dec << (int)inst_r[iq_br].uop.rob_idx << endl;
        cout << dec << (int)inst_r[iq_br].uop.rob_flag << endl;
      }
      if (!mispred)
      {
        mispred = true;
        mispred_uop = &inst_r[iq_br].uop;
      }
      else if (cmp_inst_age(*mispred_uop, inst_r[iq_br].uop))
      {
        mispred_uop = &inst_r[iq_br].uop;
      }
    }
  }

  out.prf2dec->mispred = mispred;
  if (mispred)
  {
    out.prf2dec->redirect_pc = mispred_uop->pc_next;
    out.prf2dec->redirect_rob_idx = mispred_uop->rob_idx;
    out.prf2dec->br_tag = mispred_uop->tag;
    if (LOG)
      cout << "PC " << hex << mispred_uop->pc << " mispredictinn redirect_pc 0x"
           << hex << out.prf2dec->redirect_pc << endl;
  }
  else
  {
    // 任意，以代码简单为准
  }
}
#ifdef CONFIG_CACHE
extern Back_Top back;
void PRF::comb_read()
{
  // bypass
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    out.prf2exe->iss_entry[i] = in.iss2prf->iss_entry[i];
    Inst_entry *entry = &out.prf2exe->iss_entry[i];

    if (entry->valid)
    {
      if (entry->uop.src1_en)
      {
        entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];
        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM; j++)
        {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = in.exe2prf->entry[j].uop.result;
        }

        if (in.cache2prf->valid && in.cache2prf->uop.dest_preg == entry->uop.src1_preg)
        {
          entry->uop.src1_rdata = load_data;
        }
      }

      if (entry->uop.src2_en)
      {
        entry->uop.src2_rdata = reg_file[entry->uop.src2_preg];
        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM; j++)
        {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = in.exe2prf->entry[j].uop.result;
        }
        if (in.cache2prf->valid && in.cache2prf->uop.dest_preg == entry->uop.src2_preg)
        {
          entry->uop.src2_rdata = load_data;
        }
      }
    }
  }
}
void PRF::comb_load()
{
  if (in.cache2prf->valid)
  {
    // uint32_t load_data=0;
    uint32_t data = in.cache2prf->data;
    if (in.cache2prf->uop.page_fault_load)
    {
      data = in.cache2prf->uop.src1_rdata + in.cache2prf->uop.imm;
    }
    else
    {
      back.stq.st2ld_fwd(in.cache2prf->addr, data, in.cache2prf->uop.rob_idx);
      int addr = in.cache2prf->uop.src1_rdata + in.cache2prf->uop.imm;
      int size = in.cache2prf->uop.func3 & 0b11;
      int offset = addr & 0b11;

      uint32_t mask = 0;
      uint32_t sign = 0;

      if (in.cache2prf->uop.amoop != AMONONE)
      {
        size = 0b10;
        offset = 0b0;
      }
      data = data >> (offset * 8);
      if (size == 0)
      {
        mask = 0xFF;
        if (data & 0x80)
          sign = 0xFFFFFF00;
      }
      else if (size == 0b01)
      {
        mask = 0xFFFF;
        if (data & 0x8000)
          sign = 0xFFFF0000;
      }
      else
      {
        mask = 0xFFFFFFFF;
      }

      data = data & mask;

      // 有符号数
      if (!(in.cache2prf->uop.func3 & 0b100))
      {
        data = data | sign;
      }
    }
    load_data = data;

    if(DCACHE_LOG){
      printf("PRF Load Data: addr=0x%08X rob_idx=%d inst=0x%08x data=0x%08x\n", in.cache2prf->addr, in.cache2prf->uop.rob_idx, in.cache2prf->uop.instruction, load_data);
    }
  }
}
#else
void PRF::comb_read()
{
  // bypass
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    out.prf2exe->iss_entry[i] = in.iss2prf->iss_entry[i];
    Inst_entry *entry = &out.prf2exe->iss_entry[i];

    if (entry->valid)
    {
      if (entry->uop.src1_en)
      {
        entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];
        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = in.exe2prf->entry[j].uop.result;
        }
      }

      if (entry->uop.src2_en)
      {
        entry->uop.src2_rdata = reg_file[entry->uop.src2_preg];
        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM + 1; j++)
        {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = in.exe2prf->entry[j].uop.result;
        }
      }
    }
  }
}
#endif

void PRF::comb_complete()
{
  for (int i = 0; i < ISSUE_WAY; i++)
  {
    if (inst_r[i].valid)
      out.prf2rob->entry[i] = inst_r[i];
    else
      out.prf2rob->entry[i].valid = false;
  }
}

void PRF::comb_awake()
{
  if (inst_r[IQ_LD].valid && inst_r[IQ_LD].uop.dest_en &&
      !inst_r[IQ_LD].uop.page_fault_load)
  {
    out.prf_awake->wake.valid = true;
    out.prf_awake->wake.preg = inst_r[IQ_LD].uop.dest_preg;
  }
  else
  {
    out.prf_awake->wake.valid = false;
  }
}

void PRF::comb_branch()
{
  if (in.dec_bcast->mispred)
  {
    for (int i = 0; i < ISSUE_WAY; i++)
    {
      if (inst_r[i].valid &&
          (in.dec_bcast->br_mask & (1 << inst_r[i].uop.tag)))
      {
        inst_r_1[i].valid = false;
      }
    }
  }
}

void PRF::comb_flush()
{
  if (in.rob_bcast->flush)
  {
    for (int i = 0; i < ISSUE_WAY; i++)
    {
      inst_r_1[i].valid = false;
    }
  }
}

void PRF::comb_write()
{
  for (int i = 0; i < ALU_NUM + 1; i++)
  {
    if (inst_r[i].valid && inst_r[i].uop.dest_en &&
        !is_page_fault(inst_r[i].uop))
    {
      reg_file_1[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }
}

void PRF::comb_pipeline()
{
  if(DCACHE_LOG){
    printf("PRF Pipeline Start stall_dcache=%d\n", stall_dcache);
  }
  for (int i = 0; i < ISSUE_WAY; i++)
  {
#ifndef CONFIG_CACHE
    if (in.exe2prf->entry[i].valid && out.prf2exe->ready[i])
    {
      inst_r_1[i] = in.exe2prf->entry[i];
    }
    else
    {
      inst_r_1[i].valid = false;
    }
#else
    if (i != IQ_LD && in.exe2prf->entry[i].valid && out.prf2exe->ready[i])
    {
      inst_r_1[i] = in.exe2prf->entry[i];
    }
    else if (i == IQ_LD && out.prf2exe->ready[i] && in.cache2prf->valid && !stall_dcache)
    {
      uint32_t addr = in.cache2prf->uop.src1_rdata + in.cache2prf->uop.imm;
      inst_r_1[i].uop = in.cache2prf->uop;
      inst_r_1[i].valid = true;
      inst_r_1[i].uop.result = load_data;
    }
    else
    {
      inst_r_1[i].valid = false;
    }
#endif
  }
}

void PRF::seq()
{
  for (int i = 0; i < PRF_NUM; i++)
  {
    reg_file[i] = reg_file_1[i];
  }

  for (int i = 0; i < ISSUE_WAY; i++)
  {
    inst_r[i] = inst_r_1[i];
  }
}
