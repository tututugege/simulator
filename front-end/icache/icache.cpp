#include "../front_IO.h"
#include "../front_module.h"
#include "../frontend.h"
#include "RISCV.h"
#include "TOP.h"
#include "config.h"
#include "cvt.h"
#include <cstdint>
#include <cstdio>
// no actual icache, just a simple simulation
#include "./include/icache_module.h"
#include "mmu_io.h"
#include <queue>
#include <MMU.h>

ICache icache;
extern MMU mmu;

extern uint32_t *p_memory;
extern Back_Top back;
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

void icache_top(struct icache_in *in, struct icache_out *out) {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    icache.reset();
    out->icache_read_ready = true;
    return;
  }
#ifdef USE_TRUE_ICACHE
  // register to keep track of whether memory is busy
  static bool mem_busy = false; 
  // register to keep track of the vaddr being handled by icache
  static uint32_t current_vaddr_reg; // the vaddr being processed by icache
  static bool     valid_reg; // whether current_vaddr_reg is valid

  // deal with "refetch" signal
  if (in->refetch) {
    // clear the icache state
    icache.set_refetch();
    valid_reg = false;
  }

  // set input for 1st pipeline stage (IFU)
  icache.io.in.pc = in->fetch_address;
  icache.io.in.ifu_req_valid = in->icache_read_valid;

  // set input for 2nd pipeline stage (IFU)
  icache.io.in.ifu_resp_ready = true; // ifu ready to receive data from icache

  // get ifu_resp from mmu (calculate last cycle)
  mmu_resp_master_t mmu_resp = mmu.io.out.mmu_ifu_resp;

  // set input for 2nd pipeline stage (MMU)
  icache.io.in.ppn = mmu_resp.ptag;
  icache.io.in.ppn_valid = mmu_resp.valid &&
                          !in->refetch &&
                          !mmu_resp.miss;
  icache.io.in.page_fault = mmu_resp.excp;

  // set input for 2nd pipeline stage (Memory)
  if (mem_busy) {
    // A memory request is ongoing, waiting for response from memory
    // In current design, memory always responds in 1 cycle
    icache.io.in.mem_resp_valid = true;
    bool mem_resp_valid = icache.io.in.mem_resp_valid;
    bool mem_resp_ready = icache.io.out.mem_resp_ready;
    if (mem_resp_valid) {
      // if 2 ^ k == ICACHE_LINE_SIZE, then mask = 0xFFFF_FFFF << k
      uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
      uint32_t cacheline_base_addr = icache.io.out.mem_req_addr & mask;
      // I-Cache receive data from memory
      for (int i = 0; i < ICACHE_LINE_SIZE / 4; i++) {
        icache.io.in.mem_resp_data[i] = p_memory[cacheline_base_addr / 4 + i];
      }
    }
  } else {
    // memory is idle and able to receive new request from icache
    icache.io.in.mem_req_ready = true;
  }

  icache.comb();

  // set input for request to mmu
  mmu.io.in.mmu_ifu_req.op_type = mmu_n::OP_FETCH;
  mmu.io.in.mmu_ifu_resp.ready = true; // ready to receive resp
  if (icache.io.out.ifu_req_ready && icache.io.in.ifu_req_valid) {
    // case1: ICache 支持新的请求，故而可以将新的 IFU 请求发送给 MMU
    mmu.io.in.mmu_ifu_req.valid = icache.io.out.ifu_req_ready && 
                                  in->icache_read_valid;
    mmu.io.in.mmu_ifu_req.vtag = in->fetch_address >> 12;
  } else if (!icache.io.out.ifu_req_ready) {
    // case 2: ICache 不支持新的请求，需要重发（replay），具体有两种可能：
    //  - icache miss，正在等待 memory 的数据返回
    //  - 上一个 mmu_ifu_req 请求返回了 miss
    mmu.io.in.mmu_ifu_req.valid = true; // replay request
    if (!valid_reg) {
      cout << "[icache_top] ERROR: valid_reg is false when replaying mmu_ifu_req" << endl;
      cout << "[icache_top] sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    mmu.io.in.mmu_ifu_req.vtag = current_vaddr_reg >> 12;
  } else {
    // case3: ICache 支持新的请求，但是 IFU 没有发送新的请求
    mmu.io.in.mmu_ifu_req.valid = false;
  }

  if (in->run_comb_only) {
    // Only run combinational logic, do not update registers. This is
    // used for BPU module, which needs to know if icache is ready
    out->icache_read_ready = icache.io.out.ifu_req_ready;
    return;
  }

  //
  // == sequential logic ==
  //

  icache.seq();

  // sequential logic for memory (mem_busy)
  bool mem_req_ready = !mem_busy;
  bool mem_req_valid = icache.io.out.mem_req_valid;
  if (mem_req_ready && mem_req_valid) {
    // send request to memory
    mem_busy = true;
  }
  bool mem_resp_valid = icache.io.in.mem_resp_valid;
  bool mem_resp_ready = icache.io.out.mem_resp_ready;
  if (mem_resp_valid && mem_resp_ready) {
    // have received response from memory
    mem_busy = false;
    icache.io.in.mem_resp_valid = false; // clear the signal after receiving
  }

  // sequential logic for output (to IFU)
  bool ifu_resp_valid = icache.io.out.ifu_resp_valid;
  bool ifu_resp_ready = icache.io.in.ifu_resp_ready;
  bool miss = icache.io.out.miss;
  if (ifu_resp_valid && ifu_resp_ready) {
    out->icache_read_complete = true;
    // in current design, miss is useless and always false when ifu_resp is
    // valid
    if (miss) {
      cout << "[icache_top] WARNING: miss is true when ifu_resp is valid" << endl;
      cout << "[icache_top] sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    // keep index within a cacheline
    uint32_t mask = ICACHE_LINE_SIZE - 1; // work for ICACHE_LINE_SIZE==2^k
    int base_idx = (current_vaddr_reg & mask) / 4; // index of the instruction in the cacheline
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (base_idx + i >= ICACHE_LINE_SIZE / 4) {
        // throw the instruction that exceeds the cacheline
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = false;
        out->inst_valid[i] = false;
        continue;
      }
      out->fetch_group[i] = icache.io.out.ifu_page_fault ?  INST_NOP : icache.io.out.rd_data[i+base_idx];
      out->page_fault_inst[i] = icache.io.out.ifu_page_fault;
      out->inst_valid[i] = true;
    }
  } else {
    out->icache_read_complete = false;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }
  }

  // sequential logic for current_vaddr_reg and valid_reg
  if (icache.io.in.ifu_req_valid && icache.io.out.ifu_req_ready) {
    // only push when ifu_req is sent to icache
    current_vaddr_reg = in->fetch_address;
    valid_reg = true;
  } else if (ifu_resp_valid && ifu_resp_ready) {
    valid_reg = false; // clear the valid_reg when ifu_resp is sent to ifu
  }

#else // simple icache model: directly read from pmem
  // able to fetch instructions within 1 cycle
  out->icache_read_complete = true;
  out->icache_read_ready = true;
  // when BPU sends a valid read request
  if (in->icache_read_valid) {
    // read instructions from pmem
    bool mstatus[32], sstatus[32];

    cvt_number_to_bit_unsigned(mstatus, back.csr.CSR_RegFile[csr_mstatus], 32);

    cvt_number_to_bit_unsigned(sstatus, back.csr.CSR_RegFile[csr_sstatus], 32);

    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = in->fetch_address + (i * 4);
      uint32_t p_addr;
      if ((back.csr.CSR_RegFile[csr_satp] & 0x80000000) &&
          back.csr.privilege != 3) {

        out->page_fault_inst[i] =
            !va2pa(p_addr, v_addr, back.csr.CSR_RegFile[csr_satp], 0, mstatus,
                   sstatus, back.csr.privilege, p_memory);
        if (out->page_fault_inst[i]) {
          out->fetch_group[i] = INST_NOP;
        } else {
          out->fetch_group[i] = p_memory[p_addr / 4];
        }
      } else {
        out->page_fault_inst[i] = false;
        out->fetch_group[i] = p_memory[v_addr / 4];
      }

      if (DEBUG_PRINT) {
        printf("[icache] pmem_address: %x\n", p_addr);
        printf("[icache] instruction : %x\n", out->fetch_group[i]);
      }
    }
  }
#endif
}
