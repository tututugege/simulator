#include "BPU/target_predictor/btb.h"
#include "oracle.h"
#include <RISCV.h>
#include <SimCpu.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <front_module.h>
#include <iostream>
#include <util.h>

using namespace std;
uint32_t *p_memory;

void SimCpu::init() {

  back.init();
  mmu.reset();

  // reset
#ifdef CONFIG_BPU
  front_in.reset = true;
  front_in.FIFO_read_enable = true;
  front_top(&front_in, &front_out);
  front_in.reset = false;
#endif
}

void SimCpu::cycle() {
  ctx.perf.cycle++;
  back.comb_csr_status(); // 获取mstatus sstatus satp

#ifdef CONFIG_MMU
  // Backend(CSR) -> mmu
  back2mmu_comb();
  // step1: fetch instructions and fill in back.in
#endif
  front_cycle();

#ifdef CONFIG_MMU
  mmu.comb_frontend(); // update mmu_ifu_resp according to new ifu_req_valid
#endif
  back.comb();

#ifdef CONFIG_MMU
  mmu.comb_backend(); // update mmu_lsu_resp according to new lsu_req_valid
  // Resquest from backend will be set in back.Back_comb()
  // Resquest from frontend will be set in front_cycle()
  mmu.comb_ptw();
#endif

  // step2: feedback to front-end
  back2front_comb();

  // // LOG ROB status
  // cout << "ROB状态: " << endl;
  // for (int i = 0; i < ROB_BANK_NUM; i++) {
  //   for (int j = 0; j < ROB_NUM / 4; j++) {
  //     Inst_entry *rob_entry = &back.rob.entry[i][j];
  //     if (rob_entry->valid) {
  //       cout << "1";
  //     } else {
  //       cout << "0";
  //     }
  //   }
  //   cout << endl;
  // }
  // // list the valid rob entries
  // for (int i = 0; i < ROB_BANK_NUM; i++) {
  //   for (int j = 0; j < ROB_NUM / 4; j++) {
  //     Inst_entry *rob_entry = &back.rob.entry[i][j];
  //     if (rob_entry->valid) {
  //       cout << "ROB Entry valid: PC= " << hex << rob_entry->uop.pc
  //            << " Inst= " << rob_entry->uop.instruction 
  //            << " rob_idx= " << dec << (int) rob_entry->uop.rob_idx
  //            << " inst_idx= " << dec << (int) rob_entry->uop.inst_idx
  //            << " pc=" << hex << rob_entry->uop.pc
  //            << " pc_next=" << hex << rob_entry->uop.pc_next
  //            << " mispred=" << rob_entry->uop.mispred
  //            << " br_taken=" << rob_entry->uop.br_taken
  //            << endl;
  //     }
  //   }
  // }

  back.seq();

#ifdef CONFIG_MMU
  mmu.seq();
#endif

  if (ctx.sim_end)
    return;

  ctx.stall = back.out.stall;
  ctx.misprediction = back.out.mispred;
  ctx.exception = back.out.flush;

  if (ctx.misprediction || ctx.exception) {
    back.number_PC = back.out.redirect_pc;
  } else if (ctx.stall) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (back.out.fire[j])
        back.in.valid[j] = false;
    }
  }
}

void SimCpu::front_cycle() {

  if (!ctx.stall || ctx.misprediction || ctx.exception) {

    front_in.FIFO_read_enable = true;
    front_in.refetch = (ctx.misprediction || ctx.exception);

#ifdef CONFIG_BPU
    front_top(&front_in, &front_out);
#else
    get_oracle(front_in, front_out);
#endif

    bool no_taken = true;
    for (int j = 0; j < FETCH_WIDTH; j++) {
      back.in.valid[j] =
          no_taken && front_out.FIFO_valid && front_out.inst_valid[j];
      back.in.pc[j] = front_out.pc[j];
      back.in.predict_next_fetch_address[j] =
          front_out.predict_next_fetch_address;
      back.in.page_fault_inst[j] = front_out.page_fault_inst[j];
      back.in.inst[j] = front_out.instructions[j];

      if (LOG && back.in.valid[j]) {
        cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" << hex
             << front_out.pc[j] << " Inst: " << back.in.inst[j] << endl;
      }

      back.in.predict_dir[j] = front_out.predict_dir[j];
      back.in.alt_pred[j] = front_out.alt_pred[j];
      back.in.altpcpn[j] = front_out.altpcpn[j];
      back.in.pcpn[j] = front_out.pcpn[j];
      for (int k = 0; k < 4; k++) { // TN_MAX = 4
        back.in.tage_idx[j][k] = front_out.tage_idx[j][k];
      }
      if (back.in.valid[j] && front_out.predict_dir[j])
        no_taken = false;
    }
  } else {

#ifdef CONFIG_BPU
    front_in.FIFO_read_enable = false;
    front_in.refetch = false;
    front_top(&front_in, &front_out);
#endif
  }
}

void SimCpu::back2front_comb() {
  front_in.FIFO_read_enable = false;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    Inst_uop *inst = &back.out.commit_entry[i].uop;
    front_in.back2front_valid[i] = back.out.commit_entry[i].valid;

    if (front_in.back2front_valid[i]) {
      front_in.predict_dir[i] = inst->pred_br_taken;
      front_in.predict_base_pc[i] = inst->pc;
      front_in.actual_dir[i] =
          (inst->type == JAL || inst->type == JALR) ? true : inst->br_taken;
      front_in.actual_target[i] = inst->pc_next;
      int br_type = BR_NONCTL;
      if (is_branch(inst->type)) {
        br_type = BR_DIRECT;
      }
      if (inst->type == JAL) {
        br_type = BR_JAL;
      }
      if (inst->type == JAL && inst->dest_en && inst->dest_areg == 1) {
        br_type = BR_CALL;
      } else if (inst->type == JALR) {
        if (inst->src1_areg == 1 && inst->dest_areg == 0 && inst->imm == 0)
          br_type = BR_RET;
        else
          br_type = BR_IDIRECT;
      }

      front_in.actual_br_type[i] = br_type;
      front_in.alt_pred[i] = inst->alt_pred;
      front_in.altpcpn[i] = inst->altpcpn;
      front_in.pcpn[i] = inst->pcpn;
      for (int j = 0; j < 4; j++) { // TN_MAX = 4
        front_in.tage_idx[i][j] = inst->tage_idx[j];
      }
    }
    // if (LOG) {
    //   cout << " valid: " << front_in.back2front_valid[i]
    //        << " 反馈给前端的分支指令PC: " << hex << inst->pc
    //        << " 预测结果: " << inst->pred_br_taken
    //        << " 实际结果: " << inst->br_taken
    //        << " 预测目标地址: " << inst->pred_br_pc
    //        << " 实际目标地址: " << inst->pc_next
    //        << " 指令: " << inst->instruction << endl;
    // }
  }

  if (back.out.mispred || back.out.flush) {
    front_in.refetch_address = back.out.redirect_pc;
  }
}

void SimCpu::back2mmu_comb() {
  mmu.io.in.state.satp = reinterpret_cast<satp_t &>(back.out.satp);
  mmu.io.in.state.mstatus = back.out.mstatus;
  mmu.io.in.state.sstatus = back.out.sstatus;
  mmu.io.in.state.privilege = mmu_n::Privilege(back.out.privilege);
  // for flush tlb:
  // - if request flush, set flush_valid = true in back-end later
  mmu.io.in.tlb_flush.flush_valid = false;

  // mmu.io.in.mmu_dcache_req = *back.out.dcache2ptw_req;
  // mmu.io.in.mmu_dcache_resp = *back.out.dcache2ptw_resp;

#if defined(CONFIG_CACHE) && defined(CONFIG_MMU)
  mmu.io.in.mmu_dcache_req = back.out.dcache2ptw_req;
  mmu.io.in.mmu_dcache_resp = back.out.dcache2ptw_resp;

  back.in.ptw2dcache_req = mmu.io.out.mmu_dcache_req;
  back.in.ptw2dcache_resp = mmu.io.out.mmu_dcache_resp;
#endif
  // printf("\nmmu.io.in.mmu_dcache_req.ready=%d
  // sim_time:%lld\n",mmu.io.in.mmu_dcache_req.ready, sim_time);
  // printf("mmu.io.in.mmu_dcache_resp.valid=%d
  // mmu.io.in.mmu_dcache_resp.miss=%d mmu.io.in.mmu_dcache_resp.data=0x%08X
  // sim_time:%lld\n",mmu.io.in.mmu_dcache_resp.valid,
  // mmu.io.in.mmu_dcache_resp.miss, mmu.io.in.mmu_dcache_resp.data, sim_time);
  // printf("back.in.ptw2dcache_req.valid=%d back.in.ptw2dcache_req.paddr=0x%08X
  // sim_time:%lld\n",back.in.ptw2dcache_req.valid,
  // back.in.ptw2dcache_req.paddr, sim_time);
  // printf("back.in.ptw2dcache_resp.ready=%d
  // sim_time:%lld\n",back.in.ptw2dcache_resp.ready, sim_time);
}

bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory) {
  uint32_t d = 24;
  uint32_t a = 25;
  uint32_t g = 26;
  uint32_t u = 27;
  uint32_t x = 28;
  uint32_t w = 29;
  uint32_t r = 30;
  uint32_t v = 31;
  bool mxr = mstatus[31 - 19];
  bool sum = mstatus[31 - 18];
  bool mprv = mstatus[31 - 17];
  uint32_t mpp = cvt_bit_to_number_unsigned(mstatus + 19 * sizeof(bool), 2);

  uint32_t pte1_addr = (satp << 12) | ((v_addr >> 20) & 0xFFC);
  uint32_t pte1_entry = p_memory[uint32_t(pte1_addr / 4)];
#ifdef CONFIG_CACHE
  uint32_t pte1_entry_cache;
  if (DCACHE_LOG) {
    printf("MMU va2pa v_addr:0x%08x satp:0x%08x pte1_addr:0x%08x\n", v_addr,
           satp, pte1_addr);
  }
  bool pte1_in_cache = dcache_read(pte1_addr, pte1_entry_cache);
  if (pte1_in_cache) {
    pte1_entry = pte1_entry_cache;
  }
#endif
  bool bit_pte1_entry[32];
  cvt_number_to_bit_unsigned(bit_pte1_entry, pte1_entry, 32);
  if (bit_pte1_entry[v] == false ||
      (bit_pte1_entry[r] == false && bit_pte1_entry[w] == true)) {
    return false;
  }

  if (bit_pte1_entry[r] == true || bit_pte1_entry[x] == true) {
    if (!((type == 0 && bit_pte1_entry[x] == true) ||
          (type == 1 && bit_pte1_entry[r] == true) ||
          (type == 2 && bit_pte1_entry[w] == true) ||
          (type == 1 && mxr == true && bit_pte1_entry[x] == true))) {
      return false;
    }

    if (privilege == 1 && sum == 0 && bit_pte1_entry[u] == true &&
        sstatus[31 - 18] == false) {
      return false;
    }

    if (privilege != 1 && mprv == 1 && mpp == 1 && sum == 0 &&
        bit_pte1_entry[u] == true && sstatus[31 - 18] == false) {
      return false;
    }

    if ((pte1_entry >> 10) % 1024 != 0) {
      return false;
    }

    if (bit_pte1_entry[a] == false ||
        (type == 2 && bit_pte1_entry[d] == false)) {
      return false;
    }

    p_addr = ((pte1_entry << 2) & 0xFFC00000) | (v_addr & 0x3FFFFF);
    return true;
  }

  uint32_t pte2_addr =
      ((pte1_entry << 2) & 0xFFFFF000) | ((v_addr >> 10) & 0xFFC);
  uint32_t pte2_entry = p_memory[uint32_t(pte2_addr / 4)];
#ifdef CONFIG_CACHE
  if (DCACHE_LOG) {
    printf("MMU va2pa v_addr:0x%08x satp:0x%08x pte2_addr:0x%08x\n", v_addr,
           satp, pte2_addr);
  }
  uint32_t pte2_entry_cache;
  bool pte2_in_cache = dcache_read(pte2_addr, pte2_entry_cache);
  if (pte2_in_cache) {
    pte2_entry = pte2_entry_cache;
  }
#endif
  bool bit_pte2_stored[32];
  cvt_number_to_bit_unsigned(bit_pte2_stored, pte2_entry, 32);

  if (bit_pte2_stored[v] == false ||
      (bit_pte2_stored[r] == false && bit_pte2_stored[w] == true))
    return false;
  if (bit_pte2_stored[r] == true || bit_pte2_stored[x] == true) {
    if ((type == 0 && bit_pte2_stored[x] == true) ||
        (type == 1 && bit_pte2_stored[r] == true) ||
        (type == 2 && bit_pte2_stored[w] == true) ||
        (type == 1 && mxr == true && bit_pte2_stored[x] == true)) {
      ;
    } else
      return false;
    if (privilege == 1 && sum == 0 && bit_pte2_stored[u] == true &&
        sstatus[31 - 18] == false)
      return false;
    if (privilege != 1 && mprv == 1 && mpp == 1 && sum == 0 &&
        bit_pte2_stored[u] == true && sstatus[31 - 18] == false)
      return false;
    if (bit_pte2_stored[a] == false ||
        (type == 2 && bit_pte2_stored[d] == false))
      return false;
    p_addr = (pte2_entry << 2) & 0xFFFFF000 | v_addr & 0xFFF;
    if (DCACHE_LOG) {
      printf("MMU va2pa success v_addr:0x%08x p_addr:0x%08x\n", v_addr, p_addr);
    }
    return true;
  }

  return false;
}
