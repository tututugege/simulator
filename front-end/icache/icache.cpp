#include "../front_IO.h"
#include "../front_module.h"
#include "../frontend.h"
#include "RISCV.h"
#include "SimCpu.h"
#include "TOP.h"
#include "cvt.h"
#include <cstdint>
#include <cstdio>
// no actual icache, just a simple simulation
#include "./include/icache_module.h"
#include <queue>

ICache icache;
// before MMU is implemented, we use a simple ppn_queue to store the ppn for
// icache
struct ppn_triple {
  uint32_t ppn;
  uint32_t vaddr;
  bool page_fault;
};
std::queue<ppn_triple> ppn_queue;

extern uint32_t *p_memory;
// #if defined(CONFIG_CACHE) && !defined(CONFIG_MMU)
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);
// #else
// bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
//            bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);
// #endif
void icache_top(struct icache_in *in, struct icache_out *out) {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    icache.reset();
    out->icache_read_ready = true;
    ppn_queue = std::queue<ppn_triple>();
    return;
  }
#ifdef USE_TRUE_ICACHE
  // TODO: implement true icache with real MMU interface
  static bool mem_busy = false;
  static bool current_valid =
      false; // whether current_vaddr and current_fault are valid

  // deal with "refetch" signal
  if (in->refetch) {
    // clear the ppn_queue
    while (!ppn_queue.empty()) {
      ppn_queue.pop();
    }
    // clear the icache state
    icache.set_refetch();
    current_valid = false;
  }

  // set input for 1st pipeline stage (IFU)
  icache.io.in.pc = in->fetch_address;
  icache.io.in.ifu_req_valid = in->icache_read_valid;

  // set input for 2nd pipeline stage (IFU)
  icache.io.in.ifu_resp_ready = true; // ifu ready to receive data from icache

  // set input for 2nd pipeline stage (MMU)
  icache.io.in.ppn = ppn_queue.empty() ? 0 : ppn_queue.front().ppn;
  icache.io.in.ppn_valid = ppn_queue.empty() ? false : true;

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
  if (in->run_comb_only) {
    // Only run combinational logic, do not update registers. This is
    // used for BPU module, which needs to know if icache is ready
    out->icache_read_ready = icache.io.out.ifu_req_ready;
    return;
  }
  icache.seq();

  // sequential logic for ppn_queue (push)
  if (icache.io.in.ifu_req_valid && icache.io.out.ifu_req_ready) {
    // only push when ifu_req is sent to icache
    uint32_t vaddr = in->fetch_address;
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, cpu.back.out.mstatus, 32);
    cvt_number_to_bit_unsigned(sstatus, cpu.back.out.sstatus, 32);
    uint32_t paddr;
    uint32_t ppn;
    bool page_fault_inst = false;
    if ((cpu.back.out.satp & 0x80000000) && cpu.back.out.privilege != 3) {
      page_fault_inst = !va2pa(paddr, vaddr, cpu.back.out.satp, 0, mstatus,
                               sstatus, cpu.back.out.privilege, p_memory);
    } else {
      paddr = vaddr;
    }
    ppn = paddr >> 12;
    ppn_queue.push({ppn, vaddr, page_fault_inst});
  }

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
  bool ppn_ready = icache.io.out.ppn_ready;
  bool ppn_valid = icache.io.in.ppn_valid;
  bool miss = icache.io.out.miss;
  static uint32_t current_vaddr;
  static bool current_fault;
  bool ppn_used = false; // whether a waiting ppn is used in this cycle
  if (ifu_resp_valid && ifu_resp_ready) {
    if (!current_valid) {
      // a valid ppn is used by icache
      current_vaddr = ppn_queue.front().vaddr;
      current_fault = ppn_queue.front().page_fault;
      ppn_used = true;
    }
    out->icache_read_complete = true;
    // in current design, miss is useless and always false when ifu_resp is
    // valid
    if (miss) {
      DEBUG_LOG("[icache_top] WARNING: miss is true when ifu_resp is valid\n");
      exit(1);
    }
    // Output PC address from icache (use current_vaddr which is the actual
    // request address)
    out->fetch_pc = current_vaddr;
    // keep index within a cacheline
    uint32_t mask = ICACHE_LINE_SIZE - 1; // work for ICACHE_LINE_SIZE==2^k
    int base_idx =
        (current_vaddr & mask) / 4; // index of the instruction in the cacheline
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (base_idx + i >= ICACHE_LINE_SIZE / 4) {
        // throw the instruction that exceeds the cacheline
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = current_fault;
        out->inst_valid[i] = false;
        continue;
      }
      out->fetch_group[i] =
          current_fault ? INST_NOP : icache.io.out.rd_data[i + base_idx];
      out->page_fault_inst[i] = current_fault;
      out->inst_valid[i] = true;
    }
    current_valid = false;
  } else {
    out->icache_read_complete = false;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }
  }

  // sequential logic for ppn_queue (pop)
  if (ppn_valid && ppn_ready) {
    // a valid ppn is used by icache
    current_vaddr = ppn_queue.front().vaddr;
    current_fault = ppn_queue.front().page_fault;
    current_valid = !ppn_used;
    ppn_queue.pop();
  }

  if (ppn_queue.size() > 2) {
    // this case is not expected to happen
    DEBUG_LOG("[icache_top] ERROR: ppn_queue size > 2\n");
    exit(1);
  }

#else // simple icache model: directly read from pmem
  // able to fetch instructions within 1 cycle
  out->icache_read_complete = true;
  out->icache_read_ready = true;
  out->fetch_pc = in->fetch_address;
  // when BPU sends a valid read request
  if (in->icache_read_valid) {
    // read instructions from pmem
    bool mstatus[32], sstatus[32];

    cvt_number_to_bit_unsigned(mstatus, back.out.mstatus, 32);

    cvt_number_to_bit_unsigned(sstatus, back.out.sstatus, 32);

    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = in->fetch_address + (i * 4);
      uint32_t p_addr;
      if ((back.out.satp & 0x80000000) && back.out.privilege != 3) {

        out->page_fault_inst[i] =
            !va2pa(p_addr, v_addr, back.out.satp, 0, mstatus, sstatus,
                   back.out.privilege, p_memory);
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
  } else {
    out->fetch_pc = 0; // Set default value when not valid
  }
#endif
}
