#include "AbstractLsu.h"
#include "BackTop.h"
#include "Csr.h"
#include "SimCpu.h"
#include "config.h"
#include "diff.h"
#include "front_IO.h"
#include "front_module.h"
#include "oracle.h"
#include "util.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

uint32_t *p_memory;

void SimCpu::commit_sync(InstInfo *inst) {
  BackTop *back = &this->back;
  if (inst->type == JALR) {
    if (inst->tma.is_ret) {
      this->ctx.perf.ret_br_num++;
    } else {
      this->ctx.perf.jalr_br_num++;
    }
  } else if (inst->type == BR) {
    this->ctx.perf.cond_br_num++;
  }

  if (inst->mispred) {
    if (inst->type == JALR) {
      if (inst->tma.is_ret) {
        this->ctx.perf.ret_mispred_num++;
        bool pred_taken = false;
        const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr && entry->valid) {
          pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        }
        if (!pred_taken) {
          this->ctx.perf.ret_dir_mispred++;
        } else {
          this->ctx.perf.ret_addr_mispred++;
        }
      } else {
        this->ctx.perf.jalr_mispred_num++;
        bool pred_taken = false;
        const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr && entry->valid) {
          pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        }
        if (!pred_taken) {
          this->ctx.perf.jalr_dir_mispred++;
        } else {
          this->ctx.perf.jalr_addr_mispred++;
        }
      }
    } else if (inst->type == BR) {
      bool pred_taken = false;
      const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr && entry->valid) {
        pred_taken = entry->pred_taken_mask[inst->ftq_offset];
      }
      if (pred_taken != inst->br_taken) {
        this->ctx.perf.cond_dir_mispred++;
      } else {
        this->ctx.perf.cond_addr_mispred++;
      }
      this->ctx.perf.cond_mispred_num++;
    }
  }

  if (inst->tma.mem_commit_is_store && !inst->page_fault_store) {
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx);
    if (!e.suppress_write) {
      this->mem_subsystem.on_commit_store(e.p_addr, e.data, e.func3);
    }
  }
}

void SimCpu::difftest_prepare(InstEntry *inst_entry, bool *skip) {
  Assert(inst_entry != nullptr && "SimCpu::difftest_prepare: inst_entry is null");
  Assert(skip != nullptr && "SimCpu::difftest_prepare: skip is null");
  BackTop *back = &this->back;
  InstInfo *inst = &inst_entry->uop;

  for (int i = 0; i < ARF_NUM; i++) {
    // With same-cycle EXU->ROB completion, commit-side architectural mapping
    // (arch_RAT_1) can point to a preg whose value is produced in this cycle's
    // comb writeback path. Use reg_file_1 to observe the up-to-date comb state.
    dut_cpu.gpr[i] = back->prf->reg_file_1[back->rename->arch_RAT_1[i]];
  }

  if (inst->tma.mem_commit_is_store && !inst->page_fault_store) {
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx);
    if (!e.suppress_write) {
      Assert(e.addr_valid && e.data_valid);
      dut_cpu.store = true;
      dut_cpu.store_addr = e.p_addr;
      if (e.func3 == 0b00)
        dut_cpu.store_data = e.data & 0xFF;
      else if (e.func3 == 0b01)
        dut_cpu.store_data = e.data & 0xFFFF;
      else
        dut_cpu.store_data = e.data;

      dut_cpu.store_data = dut_cpu.store_data << (dut_cpu.store_addr & 0b11) * 8;
    } else {
      dut_cpu.store = false;
    }
  } else {
    dut_cpu.store = false;
  }

  for (int i = 0; i < CSR_NUM; i++) {
    dut_cpu.csr[i] = back->csr->CSR_RegFile_1[i];
  }
  dut_cpu.pc = (is_branch(inst->type) || inst->type == JAL ||
                back->rob->out.rob_bcast->flush)
                   ? inst_entry->uop.diag_val
                   : inst->dbg.pc + 4;
  dut_cpu.instruction = inst->dbg.instruction;
  dut_cpu.page_fault_inst = inst->page_fault_inst;
  dut_cpu.page_fault_load = inst->page_fault_load;
  dut_cpu.page_fault_store = inst->page_fault_store;
  dut_cpu.inst_idx = inst->dbg.inst_idx;
  *skip = inst->dbg.difftest_skip;
}

void SimContext::run_commit_inst(InstEntry *inst_entry) {
  Assert(cpu != nullptr && "SimContext::run_commit_inst: cpu is null");
  Assert(inst_entry != nullptr &&
         "SimContext::run_commit_inst: inst_entry is null");
  Assert(inst_entry->valid &&
         "SimContext::run_commit_inst: inst_entry is not valid");
  cpu->commit_sync(&inst_entry->uop);
}

void SimContext::run_difftest_inst(InstEntry *inst_entry) {
  Assert(cpu != nullptr && "SimContext::run_difftest_inst: cpu is null");
  Assert(inst_entry != nullptr &&
         "SimContext::run_difftest_inst: inst_entry is null");
  Assert(inst_entry->valid &&
         "SimContext::run_difftest_inst: inst_entry is not valid");
  bool skip = false;
  cpu->difftest_prepare(inst_entry, &skip);
  if (skip) {
    difftest_skip();
  } else {
    difftest_step(true);
  }
}

// 复位逻辑
void SimCpu::init() {
  // 第一阶段：绑定顶层上下文
  ctx.cpu = this;

  // 第二阶段：构建模块对象（生成内部子模块实例）
  back.init();

  // 第三阶段：集中完成跨模块连线
  mem_subsystem.csr = back.csr;
  mem_subsystem.memory = p_memory;

  front.in.csr_status = back.csr->out.csr_status;
  front.ctx = &ctx;

  back.lsu->ptw_walk_port = mem_subsystem.dtlb_walk_port;
  back.lsu->ptw_mem_port = mem_subsystem.dtlb_ptw_port;

  mem_subsystem.lsu_req_io = back.lsu_dcache_req_io;
  mem_subsystem.lsu_wreq_io = back.lsu_dcache_wreq_io;
  mem_subsystem.lsu_resp_io = back.lsu_dcache_resp_io;
  mem_subsystem.lsu_wready_io = back.lsu_dcache_wready_io;

  front.icache_ptw_walk_port = mem_subsystem.itlb_walk_port;
  front.icache_ptw_mem_port = mem_subsystem.itlb_ptw_port;
#ifdef CONFIG_BPU
  front.icache_mem_read_port = mem_subsystem.icache_read_port();
#else
  // The oracle frontend does not step the icache model, so keep the AXI read
  // port disconnected in non-BPU builds.
  front.icache_mem_read_port = nullptr;
#endif

  // 第四阶段：统一执行各模块复位逻辑
  // 先初始化内存子系统，确保 front.init()/front.step_bpu() 期间若访问 icache AXI
  // 端口时，互连/DDRx/MMIO 后端已经完成 init，不会命中未初始化握手状态。
  mem_subsystem.init();
  front.init();
  oracle_pending_valid = false;
  oracle_pending_out = {};
}

// 强制重置前端 PC (用于 FAST 模式切换)
void SimCpu::restore_pc(uint32_t pc) {
  front.in.reset = false;
  front.in.FIFO_read_enable = false;
  // 显式给前端一个重取指请求，并同步后端重定向输出，
  // 保证即使调用方未额外设置 flush/mispred，也会从目标 PC 重新开始。
  front.in.refetch = true;
  front.in.refetch_address = pc;
  back.out.flush = true;
  back.out.mispred = true;
  back.out.redirect_pc = pc;

  // 刷新 CSR 状态输出 (SATP, Privilege) 以确保 MMU 模式正确
  back.comb_csr_status();
}

void SimCpu::cycle() {
  ctx.perf.cycle++;
  // 统一在此处刷新 CSR 状态，供本拍 front/back 组合逻辑共同使用。
  back.comb_csr_status();

  front_cycle();
  back.comb();
  mem_subsystem.comb();

  // 步骤 2：反馈给前端
  back2front_comb();
  back.seq();
  mem_subsystem.seq();

  if (ctx.exit_reason != ExitReason::NONE) {
    printf("Simulation Exited with Reason: %d\n", (int)ctx.exit_reason);
    return;
  }

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
  auto perf_account_front_supply = [&]() {
    if (front.in.FIFO_read_enable) {
      ctx.perf.front2back_read_enable_cycle_total++;
    }
    if (front.out.FIFO_valid) {
      ctx.perf.front2back_read_cycle_total++;
    }
    if (front.in.FIFO_read_enable && !front.out.FIFO_valid) {
      ctx.perf.front2back_read_empty_cycle_total++;
    }
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (back.in.valid[j]) {
        ctx.perf.front2back_fetched_inst_total++;
      }
    }
  };

#ifdef CONFIG_BPU
  if (!back.out.stall || back.out.mispred || back.out.flush) {

    front.in.FIFO_read_enable = true;
    front.in.refetch = (back.out.mispred || back.out.flush);
    front.in.itlb_flush = back.out.itlb_flush;
    if (front.in.refetch) {
      front.in.refetch_address =
          back.out.redirect_pc; // 再次确保赋值，防止时序错位
    }

#ifdef CONFIG_BPU
    front.step_bpu();
#endif

    bool no_taken = true;
    for (int j = 0; j < FETCH_WIDTH; j++) {
      back.in.valid[j] =
          no_taken && front.out.FIFO_valid && front.out.inst_valid[j];
      back.in.pc[j] = front.out.pc[j];
      back.in.predict_next_fetch_address[j] =
          front.out.predict_next_fetch_address;
      back.in.page_fault_inst[j] = front.out.page_fault_inst[j];
      back.in.inst[j] = front.out.instructions[j];

      if (LOG && back.in.valid[j]) {
        cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" << hex
             << front.out.pc[j] << " Inst: " << back.in.inst[j] << endl;
      }

      back.in.predict_dir[j] = front.out.predict_dir[j];
      back.in.alt_pred[j] = front.out.alt_pred[j];
      back.in.altpcpn[j] = front.out.altpcpn[j];
      back.in.pcpn[j] = front.out.pcpn[j];
      for (int k = 0; k < 4; k++) { // TN_MAX = 4 (分支预测相关索引)
        back.in.tage_idx[j][k] = front.out.tage_idx[j][k];
        back.in.tage_tag[j][k] = front.out.tage_tag[j][k];
      }
      if (back.in.valid[j] && front.out.predict_dir[j])
        no_taken = false;
    }
    perf_account_front_supply();
  } else {

#ifdef CONFIG_BPU
    front.in.FIFO_read_enable = false;
    front.in.refetch = false;
    front.in.itlb_flush = back.out.itlb_flush;
    front.step_bpu();
#else
#endif
  }
#else
  // Oracle 模式：每拍都执行握手，利用 1-entry pending 防止“当拍后端阻塞”丢指令。
  front.in.FIFO_read_enable = true;
  front.in.refetch = (back.out.mispred || back.out.flush);
  front.in.itlb_flush = back.out.itlb_flush;
  if (front.in.refetch) {
    front.in.refetch_address = back.out.redirect_pc;
  }

  // 上一拍后端非阻塞，认为 pending 已被接收。
  if (oracle_pending_valid && !back.out.stall) {
    oracle_pending_valid = false;
  }
  // 重定向优先：丢弃旧 pending，立即让 oracle 同步到新 PC。
  if (front.in.refetch) {
    oracle_pending_valid = false;
  }

  if (!oracle_pending_valid) {
    front.step_oracle();
    oracle_pending_out = front.out;
    oracle_pending_valid = true;
  } else {
    front.out = oracle_pending_out;
  }

  bool no_taken = true;
  for (int j = 0; j < FETCH_WIDTH; j++) {
    back.in.valid[j] = no_taken && front.out.FIFO_valid && front.out.inst_valid[j];
    back.in.pc[j] = front.out.pc[j];
    back.in.predict_next_fetch_address[j] = front.out.predict_next_fetch_address;
    back.in.page_fault_inst[j] = front.out.page_fault_inst[j];
    back.in.inst[j] = front.out.instructions[j];
    back.in.predict_dir[j] = front.out.predict_dir[j];
    back.in.alt_pred[j] = front.out.alt_pred[j];
    back.in.altpcpn[j] = front.out.altpcpn[j];
    back.in.pcpn[j] = front.out.pcpn[j];
    back.in.sc_used[j] = front.out.sc_used[j];
    back.in.sc_pred[j] = front.out.sc_pred[j];
    back.in.sc_sum[j] = front.out.sc_sum[j];
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      back.in.sc_idx[j][t] = front.out.sc_idx[j][t];
    }
    back.in.loop_used[j] = front.out.loop_used[j];
    back.in.loop_hit[j] = front.out.loop_hit[j];
    back.in.loop_pred[j] = front.out.loop_pred[j];
    back.in.loop_idx[j] = front.out.loop_idx[j];
    back.in.loop_tag[j] = front.out.loop_tag[j];
    for (int k = 0; k < 4; k++) {
      back.in.tage_idx[j][k] = front.out.tage_idx[j][k];
      back.in.tage_tag[j][k] = front.out.tage_tag[j][k];
    }
    if (back.in.valid[j] && front.out.predict_dir[j]) {
      no_taken = false;
    }
  }
  perf_account_front_supply();
#endif
}

void SimCpu::back2front_comb() {
  front.in.FIFO_read_enable = false;
  front.in.csr_status = back.csr->out.csr_status;
  front.in.itlb_flush = back.out.itlb_flush;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    InstInfo *inst = &back.out.commit_entry[i].uop;
    front.in.back2front_valid[i] = back.out.commit_entry[i].valid;
    for (int j = 0; j < 4; j++) {
      front.in.tage_tag[i][j] = 0;
    }
    front.in.sc_used[i] = false;
    front.in.sc_pred[i] = false;
    front.in.sc_sum[i] = 0;
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      front.in.sc_idx[i][t] = 0;
    }
    front.in.loop_used[i] = false;
    front.in.loop_hit[i] = false;
    front.in.loop_pred[i] = false;
    front.in.loop_idx[i] = 0;
    front.in.loop_tag[i] = 0;

    if (front.in.back2front_valid[i]) {

      bool pred_taken = false;
      bool alt_pred = false;
      uint8_t altpcpn = 0;
      uint8_t pcpn = 0;
      uint32_t tage_idx[4] = {0};
      uint32_t tage_tag[4] = {0};
      bool sc_used = false;
      bool sc_pred = false;
      int16_t sc_sum = 0;
      uint16_t sc_idx[BPU_SCL_META_NTABLE] = {0};
      bool loop_used = false;
      bool loop_hit = false;
      bool loop_pred = false;
      uint16_t loop_idx = 0;
      uint16_t loop_tag = 0;

      const FTQEntry *entry = back.pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr && entry->valid) {
        pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        alt_pred = entry->alt_pred[inst->ftq_offset];
        altpcpn = entry->altpcpn[inst->ftq_offset];
        pcpn = entry->pcpn[inst->ftq_offset];
        sc_used = entry->sc_used[inst->ftq_offset];
        sc_pred = entry->sc_pred[inst->ftq_offset];
        sc_sum = entry->sc_sum[inst->ftq_offset];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          sc_idx[t] = entry->sc_idx[inst->ftq_offset][t];
        }
        loop_used = entry->loop_used[inst->ftq_offset];
        loop_hit = entry->loop_hit[inst->ftq_offset];
        loop_pred = entry->loop_pred[inst->ftq_offset];
        loop_idx = entry->loop_idx[inst->ftq_offset];
        loop_tag = entry->loop_tag[inst->ftq_offset];
        for (int k = 0; k < 4; k++) {
          tage_idx[k] = entry->tage_idx[inst->ftq_offset][k];
          tage_tag[k] = entry->tage_tag[inst->ftq_offset][k];
        }
      }

      front.in.predict_dir[i] = pred_taken;
      front.in.predict_base_pc[i] = inst->dbg.pc;
      front.in.actual_dir[i] =
          (inst->type == JAL || inst->type == JALR) ? true : inst->br_taken;
      front.in.actual_target[i] =
          (is_branch(inst->type) || inst->type == JAL || inst->type == JALR)
              ? back.out.commit_entry[i].uop.diag_val
              : inst->dbg.pc + 4;
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
        if (inst->tma.is_ret)
          br_type = BR_RET;
        else
          br_type = BR_IDIRECT;
      }

      front.in.actual_br_type[i] = br_type;
      front.in.alt_pred[i] = alt_pred;
      front.in.altpcpn[i] = altpcpn;
      front.in.pcpn[i] = pcpn;
      front.in.sc_used[i] = sc_used;
      front.in.sc_pred[i] = sc_pred;
      front.in.sc_sum[i] = sc_sum;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        front.in.sc_idx[i][t] = sc_idx[t];
      }
      front.in.loop_used[i] = loop_used;
      front.in.loop_hit[i] = loop_hit;
      front.in.loop_pred[i] = loop_pred;
      front.in.loop_idx[i] = loop_idx;
      front.in.loop_tag[i] = loop_tag;
      for (int j = 0; j < 4; j++) { // TN_MAX = 4 (分支预测相关索引)
        front.in.tage_idx[i][j] = tage_idx[j];
        front.in.tage_tag[i][j] = tage_tag[j];
      }
    }
  }

  if (back.out.mispred || back.out.flush) {
    front.in.refetch_address = back.out.redirect_pc;
  }
}
