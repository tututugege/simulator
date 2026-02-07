#include "BPU/target_predictor/btb.h"
#include <BackTop.h>
#include <RISCV.h>
#include <SimCpu.h>
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <diff.h>
#include <front_IO.h>
#include <front_module.h>
#include <oracle.h>
#include <util.h>

uint32_t *p_memory;

void SimCpu::init() {

  back.init();
  mmu.reset();

  // 复位逻辑
#ifdef CONFIG_BPU
  front_in.reset = true;
  front_in.FIFO_read_enable = true;
  front_top(&front_in, &front_out);
  front_in.reset = false;
#endif
}

// 强制重置前端 PC (用于 FAST 模式切换)
void SimCpu::restore_pc(uint32_t pc) {
  front_in.reset = false;
  front_in.FIFO_read_enable =
      false; // [Fix] Don't pop instruction here, save it for Cycle 0
  front_in.refetch = true;
  front_in.fence_i = true; // 强制刷新 ICache
  front_in.refetch_address = pc;

  // [Fix] 刷新 CSR 状态输出 (SATP, Privilege) 以确保 MMU 模式正确
  back.comb_csr_status();

  // 运行一次前端逻辑以应用 PC
#ifdef CONFIG_BPU
  front_top(&front_in, &front_out);
#else
  get_oracle(front_in, front_out);
#endif

  // 清除 flags
  front_in.refetch = false;
  front_in.fence_i = false;
}

void SimCpu::cycle() {
  ctx.perf.cycle++;
  back.comb_csr_status(); // 获取mstatus sstatus satp

#ifdef CONFIG_MMU
  // 后端 (CSR) -> MMU
  back2mmu_comb();
  // 步骤 1：取指并填充后端输入
#endif
  front_cycle();

#ifdef CONFIG_MMU
  mmu.comb_frontend(); // 根据新的取指请求有效位更新 MMU 取指响应
#endif
  back.comb();

#ifdef CONFIG_MMU
  mmu.comb_backend(); // 根据新的访存请求有效位更新 MMU 访存响应
  // 后端的请求会在 back.Back_comb() 中设置
  // 前端的请求会在 front_cycle() 中设置
  mmu.comb_ptw();
#endif

  // 步骤 2：反馈给前端
  back2front_comb();
  back.seq();

#ifdef CONFIG_MMU
  mmu.seq();
#endif

  if (ctx.sim_end)
    return;

  if (back.out.mispred || back.out.flush) {
    back.number_PC = back.out.redirect_pc;
  } else if (back.out.stall) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (back.out.fire[j])
        back.in.valid[j] = false;
    }
  }
}

void SimCpu::front_cycle() {

  if (!back.out.stall || back.out.mispred || back.out.flush) {

    front_in.FIFO_read_enable = true;
    front_in.refetch = (back.out.mispred || back.out.flush);
    front_in.fence_i = back.out.fence_i;
    front_in.is_mispred = back.out.mispred && !back.out.flush; // 纯分支误预测
    front_in.is_rob_flush = back.out.flush; // ROB flush (exception/CSR/fence)
    if (front_in.refetch) {
      front_in.refetch_address =
          back.out.redirect_pc; // 再次确保赋值，防止时序错位
    }

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
      for (int k = 0; k < 4; k++) { // TN_MAX = 4 (分支预测相关索引)
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
    InstUop *inst = &back.out.commit_entry[i].uop;
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
      for (int j = 0; j < 4; j++) { // TN_MAX = 4 (分支预测相关索引)
        front_in.tage_idx[i][j] = inst->tage_idx[j];
      }
    }
  }

  if (back.out.mispred || back.out.flush) {
    front_in.refetch_address = back.out.redirect_pc;
  }
}

void SimCpu::back2mmu_comb() {
  // mmu.io.in.state.satp = reinterpret_cast<satp_t &>(back.out.satp);
  std::memcpy(&mmu.io.in.state.satp, &back.out.satp, sizeof(satp_t));
  mmu.io.in.state.mstatus = back.out.mstatus;
  mmu.io.in.state.sstatus = back.out.sstatus;
  mmu.io.in.state.privilege = mmu_n::Privilege(back.out.privilege);
  // 用于刷新 TLB：
  // - 如果请求刷新，稍后在后端设置 flush_valid = true
  mmu.io.in.tlb_flush.flush_valid = false;
}
