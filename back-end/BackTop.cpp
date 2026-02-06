#include "BackTop.h"
#include "Csr.h"
#include "IO.h"
#include "SimpleLsu.h"
#include "config.h"
#include "diff.h"
#include "oracle.h"
#include "ref.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <zlib.h>

extern RefCpu ref_cpu;

void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory);

void BackTop::difftest_cycle() {
#ifdef CONFIG_DIFFTEST
  int commit_indices[COMMIT_WIDTH];
  int commit_num = 0;
  bool skip = false;

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (rob->out.rob_commit->commit_entry[i].valid) {
      commit_indices[commit_num] = i;
      commit_num++;
      if (rob->out.rob_commit->commit_entry[i].uop.difftest_skip) {
        skip = true;
      }
    }
  }

  if (commit_num > 0) {
    for (int i = 0; i < commit_num; i++) {
      InstUop *inst = &rob->out.rob_commit->commit_entry[commit_indices[i]].uop;

      // 1. 同步侧效应 (Sync side effects for EVERY instruction)
      difftest_sync(inst);

      // 2. 如果是最后一条指令，填充完整的 dut_cpu 状态用于比对
      if (i == commit_num - 1) {
        for (int j = 0; j < ARF_NUM; j++) {
          dut_cpu.gpr[j] = prf->reg_file[rename->arch_RAT_1[j]];
        }
        for (int k = 0; k < CSR_NUM; k++) {
          dut_cpu.csr[k] = csr->CSR_RegFile_1[k];
        }

        if (is_store(*inst)) {
          StqEntry e = lsu->get_stq_entry(inst->stq_idx);
          Assert(e.addr_valid && e.data_valid);
          dut_cpu.store = true;
          dut_cpu.store_addr = e.p_addr;
          if (e.func3 == 0b00)
            dut_cpu.store_data = e.data & 0xFF;
          else if (e.func3 == 0b01)
            dut_cpu.store_data = e.data & 0xFFFF;
          else
            dut_cpu.store_data = e.data;
          dut_cpu.store_data = dut_cpu.store_data
                               << (dut_cpu.store_addr & 0b11) * 8;
        } else {
          dut_cpu.store = false;
        }

        dut_cpu.pc = inst->pc_next;
        dut_cpu.instruction = inst->instruction;
        dut_cpu.page_fault_inst = inst->page_fault_inst;
        dut_cpu.page_fault_load = inst->page_fault_load;
        dut_cpu.page_fault_store = inst->page_fault_store;
        dut_cpu.inst_idx = inst->inst_idx;

        if (skip)
          difftest_skip();
        else
          difftest_step(true);
      } else {
        // 中间指令只步进模型，不触发 checkregs
        difftest_step(false);
      }
    }
  }
#endif
}

void BackTop::difftest_sync(InstUop *inst) {
  if (inst->type == JALR) {
    if (inst->src1_areg == 1 && inst->dest_areg == 0 && inst->imm == 0) {
      ctx->perf.ret_br_num++;
    } else {
      ctx->perf.jalr_br_num++;
    }
  } else if (inst->type == BR) {
    ctx->perf.cond_br_num++;
  }

  if (inst->mispred) {
    if (inst->type == JALR) {
      if (inst->src1_areg == 1 && inst->dest_areg == 0 && inst->imm == 0) {
        ctx->perf.ret_mispred_num++;
        if (!inst->pred_br_taken) {
          ctx->perf.ret_dir_mispred++;
        } else {
          ctx->perf.ret_addr_mispred++;
        }
      } else {
        ctx->perf.jalr_mispred_num++;
        if (!inst->pred_br_taken) {
          ctx->perf.jalr_dir_mispred++;
        } else {
          ctx->perf.jalr_addr_mispred++;
        }
      }
    } else if (inst->type == BR) {
      if (inst->pred_br_taken != inst->br_taken) {
        ctx->perf.cond_dir_mispred++;
      } else {
        ctx->perf.cond_addr_mispred++;
      }
      ctx->perf.cond_mispred_num++;
    }
  }

  if (is_store(*inst)) {
    StqEntry e = lsu->get_stq_entry(inst->stq_idx);

    // --- UART/PLIC Cheat Synchronization (matching ref.cpp) ---
    if (e.p_addr == UART_ADDR_BASE) {
      p_memory[UART_ADDR_BASE / 4] = p_memory[UART_ADDR_BASE / 4] & 0xffffff00;
    }

    if (e.p_addr == UART_ADDR_BASE + 1) {
      uint8_t cmd = e.data & 0xff;
      if (cmd == 7) {
        csr->CSR_RegFile_1[csr_mip] = csr->CSR_RegFile[csr_mip] | (1 << 9);
        csr->CSR_RegFile_1[csr_sip] = csr->CSR_RegFile[csr_sip] | (1 << 9);
        p_memory[PLIC_CLAIM_ADDR / 4] = 0xa;
        p_memory[UART_ADDR_BASE / 4] = p_memory[UART_ADDR_BASE / 4] & 0xfff0ffff;
      } else if (cmd == 5) {
        p_memory[UART_ADDR_BASE / 4] =
            (p_memory[UART_ADDR_BASE / 4] & 0xfff0ffff) | 0x00030000;
      }
    }

    if (e.p_addr == PLIC_CLAIM_ADDR && (e.data & 0x000000ff) == 0xa) {
      csr->CSR_RegFile_1[csr_mip] = csr->CSR_RegFile[csr_mip] & ~(1 << 9);
      csr->CSR_RegFile_1[csr_sip] = csr->CSR_RegFile[csr_sip] & ~(1 << 9);
      p_memory[PLIC_CLAIM_ADDR / 4] = 0x0;
    }
  }
}

void BackTop::difftest_inst(InstUop *inst) {
  difftest_sync(inst);

#ifdef CONFIG_DIFFTEST
  for (int i = 0; i < ARF_NUM; i++) {
    dut_cpu.gpr[i] = prf->reg_file[rename->arch_RAT_1[i]];
  }

  if (is_store(*inst)) {
    StqEntry e = lsu->get_stq_entry(inst->stq_idx);
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
  } else
    dut_cpu.store = false;

  for (int i = 0; i < CSR_NUM; i++) {
    dut_cpu.csr[i] = csr->CSR_RegFile_1[i];
  }
  dut_cpu.pc = inst->pc_next;
  dut_cpu.instruction = inst->instruction;
  dut_cpu.page_fault_inst = inst->page_fault_inst;
  dut_cpu.page_fault_load = inst->page_fault_load;
  dut_cpu.page_fault_store = inst->page_fault_store;
  dut_cpu.inst_idx = inst->inst_idx;

  if (inst->difftest_skip) {
    difftest_skip();
  } else {
    difftest_step(true);
  }
#endif
}

void BackTop::init() {

  idu = new Idu(ctx, MAX_BR_PER_CYCLE);
  rename = new Ren(ctx);
  dis = new Dispatch(ctx);
  isu = new Isu(ctx);
  prf = new Prf(ctx);
  exu = new Exu(ctx);
  csr = new Csr();
  rob = new Rob(ctx);
  lsu = new SimpleLsu(ctx);
  lsu->set_csr(csr);

  idu->out.dec2front = &dec2front;
  idu->out.dec2ren = &dec2ren;
  idu->out.dec_bcast = &dec_bcast;
  idu->in.front2dec = &front2dec;
  idu->in.ren2dec = &ren2dec;
  idu->in.prf2dec = &prf2dec;
  idu->in.rob_bcast = &rob_bcast;
  idu->in.commit = &rob_commit;

  rename->in.dec2ren = &dec2ren;
  rename->in.dis2ren = &dis2ren;
  rename->in.iss_awake = &iss_awake;
  rename->in.prf_awake = &prf_awake;
  rename->in.dec_bcast = &dec_bcast;
  rename->in.rob_bcast = &rob_bcast;
  rename->in.rob_commit = &rob_commit;

  rename->out.ren2dec = &ren2dec;
  rename->out.ren2dis = &ren2dis;

  dis->out.dis2ren = &dis2ren;
  dis->out.dis2iss = &dis2iss;
  dis->out.dis2rob = &dis2rob;
  dis->out.dis2lsu = &dis2lsu;

  dis->in.lsu2dis = &lsu2dis;
  dis->in.ren2dis = &ren2dis;
  dis->in.iss2dis = &iss2dis;
  dis->in.rob2dis = &rob2dis;
  dis->in.prf_awake = &prf_awake;
  dis->in.iss_awake = &iss_awake;
  dis->in.rob_bcast = &rob_bcast;
  dis->in.dec_bcast = &dec_bcast;

  isu->in.rob_bcast = &rob_bcast;
  isu->in.dec_bcast = &dec_bcast;
  isu->in.dis2iss = &dis2iss;
  isu->in.prf_awake = &prf_awake;
  isu->in.exe2iss = &exe2iss;

  isu->out.iss2dis = &iss2dis;
  isu->out.iss2prf = &iss2prf;
  isu->out.iss_awake = &iss_awake;

  prf->out.prf2rob = &prf2rob;
  prf->out.prf_awake = &prf_awake;
  prf->out.prf2dec = &prf2dec;
  prf->out.prf2exe = &prf2exe;

  prf->in.iss2prf = &iss2prf;
  prf->in.exe2prf = &exe2prf;
  prf->in.dec_bcast = &dec_bcast;
  prf->in.rob_bcast = &rob_bcast;

  exu->in.prf2exe = &prf2exe;
  exu->in.dec_bcast = &dec_bcast;
  exu->in.rob_bcast = &rob_bcast;
  exu->in.lsu2exe = &lsu2exe;
  exu->in.csr2exe = &csr2exe;
  exu->in.lsu2exe = &lsu2exe;

  exu->out.exe2prf = &exe2prf;
  exu->out.exe2iss = &exe2iss;
  exu->out.exe2lsu = &exe2lsu;
  exu->out.exe2csr = &exe2csr;

  rob->in.dis2rob = &dis2rob;
  rob->in.dec_bcast = &dec_bcast;
  rob->in.prf2rob = &prf2rob;
  rob->in.dec_bcast = &dec_bcast;
  rob->in.csr2rob = &csr2rob;

  rob->out.rob_bcast = &rob_bcast;
  rob->out.rob_commit = &rob_commit;
  rob->out.rob2dis = &rob2dis;
  rob->out.rob2csr = &rob2csr;

  csr->in.exe2csr = &exe2csr;
  csr->in.rob2csr = &rob2csr;
  csr->in.rob_bcast = &rob_bcast;

  csr->out.csr2exe = &csr2exe;
  csr->out.csr2rob = &csr2rob;
  csr->out.csr2front = &csr2front;
  csr->out.csr_status = &csr_status;

  lsu->in.dis2lsu = &dis2lsu;
  lsu->in.exe2lsu = &exe2lsu;
  lsu->in.csr_status = &csr_status;
  lsu->in.rob_bcast = &rob_bcast;
  lsu->in.dec_bcast = &dec_bcast;
  lsu->in.rob_commit = &rob_commit;

  lsu->out.lsu2exe = &lsu2exe;
  lsu->out.lsu2dis = &lsu2dis;

  idu->init();
  rename->init();
  isu->init();
  prf->init();
  exu->init();
  csr->init();
  rob->init();
  lsu->init();
}

void BackTop::comb_csr_status() {
  csr->comb_csr_status();
  out.sstatus = csr->out.csr_status->sstatus;
  out.mstatus = csr->out.csr_status->mstatus;
  out.satp = csr->out.csr_status->satp;
  out.privilege = csr->out.csr_status->privilege;
}

void BackTop::comb() {
  comb_csr_status(); // Update CSR Status (SATP, Privilege) for MMU
  // 输出提交的指令
  for (int i = 0; i < FETCH_WIDTH; i++) {
    idu->in.front2dec->valid[i] = in.valid[i];
    idu->in.front2dec->pc[i] = in.pc[i];
    idu->in.front2dec->inst[i] = in.inst[i];
    idu->in.front2dec->predict_dir[i] = in.predict_dir[i];
    idu->in.front2dec->alt_pred[i] = in.alt_pred[i];
    idu->in.front2dec->altpcpn[i] = in.altpcpn[i];
    idu->in.front2dec->pcpn[i] = in.pcpn[i];
    idu->in.front2dec->predict_next_fetch_address[i] =
        in.predict_next_fetch_address[i];
    idu->in.front2dec->page_fault_inst[i] = in.page_fault_inst[i];
    for (int j = 0; j < 4; j++) { // TN_MAX = 4
      idu->in.front2dec->tage_idx[i][j] = in.tage_idx[i][j];
    }
  }

  // 每个空行表示分层  下层会依赖上层产生的某个信号
  idu->comb_decode();
  prf->comb_br_check();
  csr->comb_interrupt();
  rename->comb_alloc();
  prf->comb_complete();
  prf->comb_awake();
  prf->comb_write();
  isu->comb_ready();
  lsu->comb_lsu2dis_info();

  idu->comb_branch();
  rob->comb_complete();

  rob->comb_ready();
  rob->comb_commit();

  idu->comb_release_tag();
  dis->comb_alloc();
  lsu->comb_commit();
  lsu->comb_load_res();

  exu->comb_to_csr();
  csr->comb_csr_read();
  exu->comb_exec();

  exu->comb_ready();
  isu->comb_issue();
  lsu->comb_recv();

  isu->comb_awake();
  isu->comb_calc_latency_next();
  csr->comb_exception();
  csr->comb_csr_write();
  prf->comb_read();
  rename->comb_wake();
  dis->comb_wake();
  rename->comb_rename();

  dis->comb_dispatch();

  dis->comb_fire();
  rename->comb_fire();
  rob->comb_fire();
  idu->comb_fire();

  lsu->comb_stq_alloc();

  // 用于调试
  // 修正pc_next 以及difftest对应的pc_next
  out.flush = rob->out.rob_bcast->flush;
  out.fence_i = rob->out.rob_bcast->fence_i;

  // 1. Normal case (No Rob flush)
  if (!rob->out.rob_bcast->flush) {
    out.mispred = prf->out.prf2dec->mispred;
    out.stall = !idu->out.dec2front->ready;
    out.redirect_pc = prf->out.prf2dec->redirect_pc;
  } else {
    out.mispred = true;
    if (rob->out.rob_bcast->mret || rob->out.rob_bcast->sret) {
      out.redirect_pc = csr->out.csr2front->epc;
    } else if (rob->out.rob_bcast->exception) {
      out.redirect_pc = csr->out.csr2front->trap_pc;
    } else {
      out.redirect_pc = rob->out.rob_bcast->pc + 4;
    }
  }

  if (LOG) {
    cout << "flush" << endl;
    cout << "redirect_pc: " << hex << out.redirect_pc << endl;
  }

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    out.commit_entry[i] = rob->out.rob_commit->commit_entry[i];
    if (out.commit_entry[i].valid && out.flush) {
      out.commit_entry[i].uop.pc_next = out.redirect_pc;
      rob->out.rob_commit->commit_entry[i].uop.pc_next = out.redirect_pc;
    }
  }

  isu->comb_enq();
  rename->comb_commit();
  rob->comb_flush();
  prf->comb_flush();
  rename->comb_flush();
  idu->comb_flush();
  isu->comb_flush();
  lsu->comb_flush();
  rob->comb_branch();
  prf->comb_branch();
  rename->comb_branch();
  prf->comb_pipeline();
  exu->comb_pipeline();
  dis->comb_pipeline();
  rename->comb_pipeline();
}

void BackTop::seq() {
  // rename -> isu/stq/rob
  // exu -> prf
  rename->seq();
  dis->seq();
  idu->seq();
  isu->seq();
  exu->seq();
  prf->seq();
  rob->seq();
  csr->seq();
  lsu->seq();
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.fire[i] = idu->out.dec2front->fire[i];
  }
}

// --- 辅助函数：简化 zlib 读写 POD 类型 ---
template <typename T> void gz_write_pod(gzFile file, const T &data) {
  if (gzwrite(file, &data, sizeof(T)) != sizeof(T)) {
    std::cerr << "Error writing data to gzip file." << std::endl;
    exit(1);
  }
}

template <typename T> void gz_read_pod(gzFile file, T &data) {
  if (gzread(file, &data, sizeof(T)) != sizeof(T)) {
    std::cerr << "Error reading data from gzip file." << std::endl;
    exit(1);
  }
}

void BackTop::load_image(const std::string &filename) {
  std::ifstream inst_data(filename, std::ios::in);
  if (!inst_data.is_open()) {
    cout << "Error: Image " << filename << " does not exist" << endl;
    exit(1);
  }

  inst_data.seekg(0, std::ios::end);
  std::streamsize size = inst_data.tellg();
  inst_data.seekg(0, std::ios::beg);

  if (!inst_data.read(reinterpret_cast<char *>(p_memory + 0x80000000 / 4),
                      size)) {
    std::cerr << "读取文件失败！" << std::endl;
    exit(1);
  }

  inst_data.close();

  p_memory[uint32_t(0x0 / 4)] = 0xf1402573;
  p_memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  p_memory[uint32_t(0x8 / 4)] = 0x800002b7;
  p_memory[uint32_t(0xc / 4)] = 0x00028067;
  p_memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关

#ifdef CONFIG_DIFFTEST
  init_difftest(size);
#endif

#ifndef CONFIG_BPU
  init_oracle(size);
#endif
}

void BackTop::restore_from_ref() {
  CPU_state state;
  uint8_t privilege;
  get_state(state, privilege, p_memory);
  number_PC = state.pc;

  csr->privilege = csr->privilege_1 = privilege;
  for (int i = 0; i < ARF_NUM; i++) {
    prf->reg_file[i] = state.gpr[i];
    prf->reg_file_1[i] = state.gpr[i];
  }

  for (int i = 0; i < CSR_NUM; i++) {
    csr->CSR_RegFile[i] = state.csr[i];
    csr->CSR_RegFile_1[i] = state.csr[i];
  }
}

void BackTop::restore_checkpoint(const std::string &filename) {

  std::string final_name = filename;
  gzFile file = gzopen(final_name.c_str(), "rb");
  if (!file && final_name.find(".gz") == std::string::npos) {
    final_name += ".gz";
    file = gzopen(final_name.c_str(), "rb");
  }

  if (!file) {
    std::cerr << "Error: Could not open file: " << filename << std::endl;
    exit(1);
  }

  CPU_state state;
  uint64_t interval_inst_count;

  // 1. 恢复状态
  gz_read_pod(file, state);
  gz_read_pod(file, interval_inst_count);

  number_PC = state.pc;
  csr->privilege = csr->privilege_1 = RISCV_MODE_U;
  for (int i = 0; i < ARF_NUM; i++) {
    prf->reg_file[i] = state.gpr[i];
    prf->reg_file_1[i] = state.gpr[i];
  }

  for (int i = 0; i < CSR_NUM; i++) {
    csr->CSR_RegFile[i] = state.csr[i];
    csr->CSR_RegFile_1[i] = state.csr[i];
  }

  // 2. 恢复内存
  if (p_memory == nullptr) {
    std::cerr << "Error: Memory not allocated." << std::endl;
    exit(1);
  }

  // [重要] 计算总字节数
  uint64_t total_bytes = (uint64_t)PHYSICAL_MEMORY_LENGTH * sizeof(uint32_t);
  uint8_t *byte_ptr = reinterpret_cast<uint8_t *>(p_memory);
  uint64_t remain = total_bytes;

  std::cout << "Restoring Memory..." << std::endl;

  const uint64_t GZ_CHUNK_SIZE = 1ULL * 1024 * 1024 * 1024;
  while (remain > 0) {
    unsigned int chunk = (remain > GZ_CHUNK_SIZE) ? (unsigned int)GZ_CHUNK_SIZE
                                                  : (unsigned int)remain;

    int read_bytes = gzread(file, byte_ptr, chunk);
    if (read_bytes < 0) {
      std::cerr << "Error: gzread failed." << std::endl;
      exit(1);
    }
    if (read_bytes == 0) {
      std::cerr << "Error: Unexpected EOF." << std::endl;
      exit(1);
    }

    byte_ptr += read_bytes;
    remain -= read_bytes;
  }

  gzclose(file);
  std::cout << "Checkpoint restored from " << final_name << std::endl;

  init_diff_ckpt(state, p_memory);
}
