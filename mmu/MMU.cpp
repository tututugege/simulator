#include <MMU.h>
#include <TLB.h>
#include <TLBEntry.h>
#include <cmath>
#include <config.h>
#include <cstdint>
#include <cstdio>
#include <iostream>

using namespace mmu_n;

/*
 * MMU Inner Module connections
 */
MMU::MMU()
    : io{}, ptw2tlb{}, tlb2ptw{},
      tlb(&ptw2tlb,           // PTW to TLB interface
          &io.in.mmu_ifu_req, // IFU Port Signals
          io.in.mmu_lsu_req,  // LSU Port Signals
          &io.in.tlb_flush,   // TLB Flush Signal
          &io.in.state.satp),
      ptw(&tlb2ptw,                // TLB to PTW interface
          &ptw2tlb,                // PTW to TLB interface
          &io.out.mmu_dcache_req,  // dcache_req master
          &io.in.mmu_dcache_req,   // dcache_req slave
          &io.in.mmu_dcache_resp,  // dcache_resp master
          &io.out.mmu_dcache_resp, // dcache_resp slave
          &io.in.state) {
  resp_ifu_r = &io.out.mmu_ifu_resp;
  resp_ifu_r_1 = {};
  resp_lsu_r = io.out.mmu_lsu_resp;
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    resp_lsu_r_1[i] = {};
  }
  // 初始化 MMU 状态
  io.in.state.privilege = M_MODE; // 默认特权级别为 M_MODE
}

void MMU::reset() {
  // reset MMU state
  io.in.state = {};               // reset entire state
  io.in.state.privilege = M_MODE; // 默认特权级别为 M_MODE

  // reset TLB
  tlb.reset();
  ptw.reset();

  // reset top-level registers
  *resp_ifu_r = {};
  resp_ifu_r_1 = {};
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    resp_lsu_r_1[i] = {};
    resp_lsu_r[i] = {};
  }
}

/*
 * MMU combinational logic -- Frontend
 * Frontend <-> MMU
 *
 * Frontend -> MMU: send mmu_req (blocking)
 * MMU -> Frontend: check if hit or not
 */
void MMU::comb_frontend() {
  /*
   * Bare mode 下不需要查询 TLB，这里只是定义默认的输出行为
   *
   * 前端在 Bare mode 下可以bypass MMU，直接将虚拟地址作为物理地址返回；
   *
   * 但是为了设计的统一性，仍然让请求经过 MMU，只是在 MMU 内部不进行地址
   * 转换，直接将虚拟地址作为物理地址返回；同样还是第2拍返回结果。
   */
  if (io.in.state.satp.mode == MODE_BARE || io.in.state.privilege == M_MODE) {
    // deal with REQ channel
    io.out.mmu_ifu_req.ready = true;
    if (io.in.mmu_ifu_req.op_type != OP_FETCH) {
      // debug point: should not happen for IFU requests
      cout << "MMU comb_frontend received non-FETCH request from "
           << "IFU in Bare mode. cycle: " << sim_time << endl;
      exit(1);
    }
    uint32_t ptag = io.in.mmu_ifu_req.vtag;
    bool ifu_valid = io.in.mmu_ifu_req.valid;
    comb_set_resp(resp_ifu_r_1, ifu_valid, false, false, ptag);

    tlb2ptw_frontend = {}; // default no miss for arbiter
    return;
  }

  /*
   * Sv32 mode 下需要查询 TLB
   */
  tlb.comb_frontend(); // lookup TLB for IFU request

  io.out.mmu_ifu_req.ready = true; // always ready to accept IFU req
  // set response
  if (tlb.ifu_io.out.hit) {
    TLBEntry entry = tlb.ifu_io.out.entry;
    // check page fault
    bool page_fault =
        entry.is_page_fault(OP_FETCH, io.in.state.mstatus, io.in.state.sstatus,
                            io.in.state.privilege);
    // TLB 命中
    uint32_t ptag = tlb.ifu_io.out.entry.get_ppn();
    if (entry.megapage) {
      uint32_t vpn0 = io.in.mmu_ifu_req.vtag & 0x3FF;
      ptag = (ptag & 0xFFFFFC00) | vpn0;
    }
    comb_set_resp(resp_ifu_r_1, tlb.ifu_io.out.valid,
                  false, // hit
                  page_fault, ptag);
    tlb2ptw_frontend = {}; // no miss for arbiter
  } else if (io.in.mmu_ifu_req.valid) {
    // TLB 未命中
    comb_set_resp(resp_ifu_r_1, tlb.ifu_io.out.valid,
                  true,  // miss
                  false, // no page fault
                  0      // ptag
    );
    tlb2ptw_frontend = {.tlb_miss = true,
                        .vpn1 = io.in.mmu_ifu_req.vtag >> 10,
                        .vpn0 = io.in.mmu_ifu_req.vtag & 0x3ff,
                        .op_type = OP_FETCH};
  } else {
    // no valid request, do nothing
    tlb2ptw_frontend = {}; // default no miss for arbiter
  }
  // 如果 TLB 命中或者没有进行查询，则下周期仍然处于 IDLE 状态
  io.out.mmu_ifu_req.ready = true; // always ready
}

/*
 * MMU combinational logic -- Backend
 * Backend <-> MMU
 *
 * Backend -> MMU: send mmu_req (non-blocking)
 * MMU -> Backend: check if hit or not
 */
void MMU::comb_backend() {
  /*
   * Bare mode 下不需要查询 TLB
   */
  if (io.in.state.satp.mode == MODE_BARE || io.in.state.privilege == M_MODE) {
    // deal with REQ channel
    for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
      io.out.mmu_lsu_req[i].ready = true;
      if (io.in.mmu_lsu_req[i].op_type != OP_LOAD &&
          io.in.mmu_lsu_req[i].op_type != OP_STORE &&
          io.in.mmu_lsu_req[i].valid) {
        // debug point: should not happen for IFU requests
        cout << "MMU comb_backend received invalid request from "
             << "LSU in Bare mode. cycle: " << sim_time << endl;
        exit(1);
      }
      uint32_t ptag = io.in.mmu_lsu_req[i].vtag;
      bool lsu_valid = io.in.mmu_lsu_req[i].valid;
      comb_set_resp(resp_lsu_r_1[i], lsu_valid,
                    false, // no miss
                    false, // no excp
                    ptag);
      tlb2ptw_backend[i] = {}; // default no miss for arbiter
    }
    return;
  }

  /*
   * Sv32 mode 下需要查询 TLB
   */
  tlb.comb_backend(); // lookup TLB for LSU requests

  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    io.out.mmu_lsu_req[i].ready = true; // always ready for LSU req
    // set response
    if (tlb.lsu_io[i].out.hit) {
      TLBEntry entry = tlb.lsu_io[i].out.entry;
      // check page fault
      bool page_fault =
          entry.is_page_fault(io.in.mmu_lsu_req[i].op_type, io.in.state.mstatus,
                              io.in.state.sstatus, io.in.state.privilege);
      // TLB 命中
      uint32_t ptag = tlb.lsu_io[i].out.entry.get_ppn();
      if (entry.megapage) {
        uint32_t vpn0 = io.in.mmu_lsu_req[i].vtag & 0x3FF;
        ptag = (ptag & 0xFFFFFC00) | vpn0;
      }
      comb_set_resp(resp_lsu_r_1[i], tlb.lsu_io[i].out.valid,
                    false, // hit
                    page_fault, ptag);
      tlb2ptw_backend[i] = {}; // no miss for arbiter
    } else if (io.in.mmu_lsu_req[i].valid) {
      // TLB 未命中
      comb_set_resp(resp_lsu_r_1[i], tlb.lsu_io[i].out.valid,
                    true,  // miss
                    false, // no page fault
                    0      // ptag
      );
      tlb2ptw_backend[i] = {.tlb_miss = true,
                            .vpn1 = io.in.mmu_lsu_req[i].vtag >> 10,
                            .vpn0 = io.in.mmu_lsu_req[i].vtag & 0x3ff,
                            .op_type = io.in.mmu_lsu_req[i].op_type};
    } else {
      // no valid request, do nothing
      tlb2ptw_backend[i] = {};
      comb_set_resp(resp_lsu_r_1[i],
                    false, // invalid
                    false, // not miss
                    false, // no page fault
                    0      // ptag
      );
    }
    // 如果 TLB 命中或者没有进行查询，则下周期仍然处于 IDLE 状态
    io.out.mmu_lsu_req[i].ready = true; // always ready
  }
}

/*
 * arbiter between frontend and backend miss requests
 * priority: backend[0] > backend[1] > ... > frontend
 */
void MMU::comb_arbiter() {
  tlb2ptw = {}; // default no miss
  for (int i = 0; i < MAX_LSU_REQ_NUM; ++i) {
    if (tlb2ptw_backend[i].tlb_miss) {
      tlb2ptw = tlb2ptw_backend[i];
      return;
    }
  }
  if (tlb2ptw_frontend.tlb_miss) {
    tlb2ptw = tlb2ptw_frontend;
    return;
  }
}

/*
 * MMU combinational logic -- PTW interaction
 * PTW <-> TLB
 */
void MMU::comb_ptw() {
  // 模拟 PTW 与 DCache 的交互
  // ptw.in.dcache_req->ready = true;  // always ready to accept dcache req
  // ptw.in.dcache_resp->valid = true; // always valid dcache resp
  // ptw.in.dcache_resp->miss = true;  // always miss for now

  // arbiter between multiple PTW requests
  comb_arbiter();

  // pass PTW signals to PTW module
  ptw.comb();

  // update tlb plru tree
  tlb.comb_arbiter();
  // TLB deal with PTW feedback signals
  tlb.comb_write();
  // process flush request from back-end
  tlb.comb_flush();
  // replacement policy update
  tlb.comb_replacement();
}

void MMU::seq() {
  /*
   * Sequential Logic for Top-Level Registers
   */
  *resp_ifu_r = resp_ifu_r_1;
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    resp_lsu_r[i] = resp_lsu_r_1[i];
  }
  /*
   * Sequential update of inner modules
   */
  tlb.seq();
  ptw.seq();
}
