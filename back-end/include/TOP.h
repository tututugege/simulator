#pragma once
#include <CSR.h>
#include <Dispatch.h>
#include <EXU.h>
#include <IDU.h>
#include <ISU.h>
#include <PRF.h>
#include <ROB.h>
#include <Rename.h>
#include <STQ.h>
#include <Dcache.h>
#include <MSHR.h>
#include <Memory.h>
#include <WriteBuffer.h>
#include <WriteBack_Arbiter.h>
#include <Arbiter.h>
#include <config.h>
#include <cstdint>

typedef struct {
  uint32_t inst[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH];
  bool valid[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  uint32_t predict_next_fetch_address[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
#if defined(CONFIG_MMU)&& defined(CONFIG_CACHE)
    dcache_resp_slave_t *ptw2dcache_resp;
    dcache_req_master_t *ptw2dcache_req;
#endif
} Back_in;

typedef struct {
  // to front-end
  bool mispred;
  bool stall;
  bool flush;
  bool fire[FETCH_WIDTH];
  uint32_t redirect_pc;
  Inst_entry commit_entry[COMMIT_WIDTH];

  wire32_t sstatus;
  wire32_t mstatus;
  wire32_t satp;
  wire2_t privilege;
#if defined(CONFIG_MMU)&& defined(CONFIG_CACHE)
    dcache_req_slave_t *dcache2ptw_req;
    dcache_resp_master_t *dcache2ptw_resp;
#endif
} Back_out;

class Back_Top {
public:
  IDU idu;
  Rename rename;
  Dispatch dis;
  ISU isu;
  PRF prf;
  EXU exu;
  CSRU csr;
  STQ stq;
  Dcache dcache;
  MSHR mshr;
  WriteBuffer writebuffer;
  Arbiter arbiter;
  WriteBack_Arbiter wb_arbiter;
  MEMORY memory;

  ROB rob;
  Back_in in;
  Back_out out;
  void init();
  void comb_csr_status();
  void comb();
  void seq();

#ifdef CONFIG_MMU
  bool load_data(uint32_t &data, uint32_t v_addr, int rob_idx,
                 bool &mmu_page_fault, uint32_t &mmu_ppn, bool &stall_load);
#else
  bool load_data(uint32_t &data, uint32_t v_addr, int rob_idx);
#endif

  // debug
  void difftest_inst(Inst_uop *inst);
  void difftest_cycle();
};

extern Back_Top back;
