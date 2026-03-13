#include "BackTop.h"
#include "Csr.h"
#include "IO.h"
#include "SimpleLsu.h"
#include "config.h"
#include "diff.h"
#include "oracle.h"
#include "ref.h"
#include "util.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <zlib.h>

void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory);

void BackTop::init() {
  pre_idu_queue = new PreIduQueue(ctx);
  idu = new Idu(ctx, MAX_BR_PER_CYCLE);
  rename = new Ren(ctx);
  dis = new Dispatch(ctx);
  isu = new Isu(ctx);
  prf = new Prf(ctx);
  exu = new Exu(ctx, &ftq_lookup);
  csr = new Csr();
  rob = new Rob(ctx);
  lsu = new SimpleLsu(ctx);
  lsu->set_csr(csr);

  pre_idu_queue->out.dec2front = &dec2front;
  pre_idu_queue->out.issue = &pre_idu_issue;
  pre_idu_queue->out.ftq_lookup = &ftq_lookup;
  pre_idu_queue->in.front2dec = &front2dec;
  pre_idu_queue->in.ren2dec = &ren2dec;
  pre_idu_queue->in.idu_dec2ren = &dec2ren;
  pre_idu_queue->in.rob_bcast = &rob_bcast;
  pre_idu_queue->in.rob_commit = &rob_commit;
  pre_idu_queue->in.exu2id = &exu2id;

  idu->out.dec2ren = &dec2ren;
  idu->out.dec_bcast = &dec_bcast;
  idu->out.ftq_lookup = &ftq_lookup;
  idu->in.issue = &pre_idu_issue;
  idu->in.ren2dec = &ren2dec;
  idu->in.rob_bcast = &rob_bcast;
  idu->in.exu2id = &exu2id;

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

  prf->out.prf2exe = &prf2exe;
  prf->out.prf_awake = &prf_awake;

  prf->in.iss2prf = &iss2prf;
  prf->in.exe2prf = &exe2prf;
  prf->in.dec_bcast = &dec_bcast;
  prf->in.rob_bcast = &rob_bcast;

  exu->in.prf2exe = &prf2exe;
  exu->in.dec_bcast = &dec_bcast;
  exu->in.rob_bcast = &rob_bcast;
  exu->in.lsu2exe = &lsu2exe;
  exu->in.csr2exe = &csr2exe;

  exu->out.exe2prf = &exe2prf;
  exu->out.exe2iss = &exe2iss;
  exu->out.exe2lsu = &exe2lsu;
  exu->out.exe2lsu = &exe2lsu;
  exu->out.exe2csr = &exe2csr;
  exu->out.exu2id = &exu2id;
  exu->out.exu2rob = &exu2rob;

  rob->in.dis2rob = &dis2rob;
  rob->in.dec_bcast = &dec_bcast;
  rob->in.lsu2rob = &lsu2rob;
  rob->in.csr2rob = &csr2rob;
  rob->in.exu2rob = &exu2rob;

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
  lsu->in.dcache_resp = &dcache2lsu_resp;
  lsu->in.dcache_wready = &dcache2lsu_wready;

  lsu->out.lsu2exe = &lsu2exe;
  lsu->out.lsu2dis = &lsu2dis;
  lsu->out.lsu2rob = &lsu2rob;
  lsu->out.dcache_req = &lsu2dcache_req;
  lsu->out.dcache_wreq = &lsu2dcache_wreq;

  pre_idu_queue->init();
  idu->init();
  rename->init();
  dis->init();
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
  // CSR 状态由 SimCpu::cycle() 在进入 front/back 组合逻辑前统一刷新。
#if CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN
  // Diagnostic mode: clear backend internal stage IOs to expose hidden
  // dependence on previous-cycle combinational values.
  dec2front = {};
  pre_idu_issue = {};
  ftq_lookup = {};

  dec2ren = {};
  dec_bcast = {};

  ren2dec = {};
  ren2dis = {};

  dis2ren = {};
  dis2iss = {};
  dis2rob = {};
  dis2lsu = {};

  iss_awake = {};
  iss2prf = {};
  iss2dis = {};

  prf2exe = {};
  prf_awake = {};

  exu2id = {};
  // Known issue path: ROB consumes exu2rob before EXU refreshes it in current
  // comb order. Keep it out of global clear for now and handle EXU separately.
  exe2prf = {};
  exe2iss = {};
  exe2lsu = {};
  exe2csr = {};

  rob2dis = {};
  rob2csr = {};
  rob_bcast = {};
  rob_commit = {};

  csr2exe = {};
  csr2rob = {};
  csr2front = {};

  lsu2exe = {};
  lsu2dis = {};
  lsu2rob = {};
  lsu2dcache_req = {};
  lsu2dcache_wreq = {};
#endif

  pre_idu_queue->comb_begin();
  // 输出提交的指令
  for (int i = 0; i < FETCH_WIDTH; i++) {
    pre_idu_queue->in.front2dec->valid[i] = in.valid[i];
    pre_idu_queue->in.front2dec->pc[i] = in.pc[i];
    pre_idu_queue->in.front2dec->inst[i] = in.inst[i];
    pre_idu_queue->in.front2dec->predict_dir[i] = in.predict_dir[i];
    pre_idu_queue->in.front2dec->alt_pred[i] = in.alt_pred[i];
    pre_idu_queue->in.front2dec->altpcpn[i] = in.altpcpn[i];
    pre_idu_queue->in.front2dec->pcpn[i] = in.pcpn[i];
    pre_idu_queue->in.front2dec->predict_next_fetch_address[i] =
        in.predict_next_fetch_address[i];
    pre_idu_queue->in.front2dec->page_fault_inst[i] = in.page_fault_inst[i];
    for (int j = 0; j < 4; j++) { // TN_MAX = 4
      pre_idu_queue->in.front2dec->tage_idx[i][j] = in.tage_idx[i][j];
      pre_idu_queue->in.front2dec->tage_tag[i][j] = in.tage_tag[i][j];
    }
  }

  // 每个空行表示分层  下层会依赖上层产生的某个信号
  pre_idu_queue->comb_accept_front();
  idu->comb_decode();
  csr->comb_interrupt();
  rename->comb_alloc();
  prf->comb_complete();
  prf->comb_awake();
  prf->comb_write();
  isu->comb_ready();
  lsu->comb_lsu2dis_info();

  idu->comb_branch();

  rob->comb_ready();
  rob->comb_commit();

  dis->comb_alloc();
  lsu->comb_load_res();

  exu->comb_to_csr();
  csr->comb_csr_read();
  exu->comb_exec();
  rob->comb_complete();

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
  pre_idu_queue->comb_consume_issue();

  // 用于调试
  // 修正pc_next 以及difftest对应的pc_next
  out.flush = rob->out.rob_bcast->flush;
  out.fence_i = rob->out.rob_bcast->fence_i;
  out.itlb_flush = rob->out.rob_bcast->fence;

  // 1. Normal case (No Rob flush)
  if (!rob->out.rob_bcast->flush) {
    out.mispred = idu->out.dec_bcast->mispred;
    out.stall = !dec2front.ready;
    out.redirect_pc = idu->br_latch.redirect_pc;
  } else {
    out.mispred = true;
    if (rob->out.rob_bcast->mret || rob->out.rob_bcast->sret) {
      out.redirect_pc = csr->out.csr2front->epc;
    } else if (rob->out.rob_bcast->exception) {
      out.redirect_pc = csr->out.csr2front->trap_pc;
    } else {
      out.redirect_pc = rob->out.rob_bcast->pc + 4;
    }
    BE_LOG("flush redirect_pc=0x%08x", (uint32_t)out.redirect_pc);
  }

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    out.commit_entry[i] = rob->out.rob_commit->commit_entry[i];
    if (out.commit_entry[i].valid && out.flush) {
      // Flush: override extra_data.pc_next with redirect_pc for ALL
      // instructions so Difftest can read the correct next-PC (trap vector,
      // epc, etc.)
      out.commit_entry[i].uop.diag_val = out.redirect_pc;
      rob->out.rob_commit->commit_entry[i].uop.diag_val = out.redirect_pc;
    }
  }

  isu->comb_enq();
  rename->comb_commit();
  rob->comb_flush();
  rename->comb_flush();
  idu->comb_flush();
  pre_idu_queue->comb_flush_recover();
  isu->comb_flush();
  lsu->comb_flush();
  pre_idu_queue->comb_commit_reclaim();
  rob->comb_branch();
  rename->comb_branch();
  prf->comb_pipeline();
  exu->comb_pipeline();
  dis->comb_pipeline();
  rename->comb_pipeline();
}

void BackTop::seq() {
  // rename -> isu/stq/rob
  // exu -> prf
  pre_idu_queue->seq();
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
    out.fire[i] = dec2front.fire[i];
  }
}

// --- 辅助函数：简化 zlib 读写 POD 类型 ---
template <typename T> void gz_write_pod(gzFile file, const T &data) {
  if (gzwrite(file, &data, sizeof(T)) != sizeof(T)) {
    Assert(0 && "Error writing data to gzip file.");
  }
}

template <typename T> void gz_read_pod(gzFile file, T &data) {
  if (gzread(file, &data, sizeof(T)) != sizeof(T)) {
    Assert(0 && "Error reading data from gzip file.");
  }
}

void BackTop::load_image(const std::string &filename) {
  std::ifstream inst_data(filename, std::ios::in);
  if (!inst_data.is_open()) {
    Assert(0 && "Error: Image does not exist");
  }

  inst_data.seekg(0, std::ios::end);
  std::streamsize size = inst_data.tellg();
  inst_data.seekg(0, std::ios::beg);

  if (!inst_data.read(reinterpret_cast<char *>(p_memory + 0x80000000 / 4),
                      size)) {
    Assert(0 && "读取文件失败！");
  }

  inst_data.close();

  p_memory[uint32_t(0x0 / 4)] = 0xf1402573;
  p_memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  p_memory[uint32_t(0x8 / 4)] = 0x800002b7;
  p_memory[uint32_t(0xc / 4)] = 0x00028067;
  p_memory[0x10000004 / 4] = 0x00006000;           // 和进入 OpenSBI 相关
  p_memory[uint32_t(0x00001000 / 4)] = 0x00000297; // auipc t0,0
  p_memory[uint32_t(0x00001004 / 4)] = 0x02828613; // addi a2,t0,40
  p_memory[uint32_t(0x00001008 / 4)] = 0xf1402573; // csrrs a0,mhartid,zero
  p_memory[uint32_t(0x0000100c / 4)] = 0x0202a583; // lw a1,32(t0)
  p_memory[uint32_t(0x00001010 / 4)] = 0x0182a283; // lw t0,24(t0)
  p_memory[uint32_t(0x00001014 / 4)] = 0x00028067; // jr              t0
  p_memory[uint32_t(0x00001018 / 4)] = 0x80000000;
  p_memory[uint32_t(0x00001020 / 4)] = 0x8fe00000;

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

  // 同步 dut_cpu，全局参考路径（oracle/refetch 校验）依赖该镜像状态。
  std::memcpy(dut_cpu.gpr, state.gpr, sizeof(dut_cpu.gpr));
  std::memcpy(dut_cpu.csr, state.csr, sizeof(dut_cpu.csr));
  dut_cpu.pc = state.pc;
  dut_cpu.store = state.store;
  dut_cpu.store_addr = state.store_addr;
  dut_cpu.store_data = state.store_data;

  // Ensure the pipeline starts with a refetch from the restored PC
  out.flush = true;
  out.redirect_pc = state.pc;

#ifndef CONFIG_BPU
  // FAST 模式从 ref 切换到 O3+oracle 时，oracle 也必须恢复到同一状态，
  // 否则首拍 refetch 会因为 PC 不一致触发断言。
  init_oracle_ckpt(state, p_memory, privilege);
#endif
}

void BackTop::restore_checkpoint(const std::string &filename) {

  std::string final_name = filename;
  gzFile file = gzopen(final_name.c_str(), "rb");
  if (!file && final_name.find(".gz") == std::string::npos) {
    final_name += ".gz";
    file = gzopen(final_name.c_str(), "rb");
  }

  if (!file) {
    Assert(0 && "Error: Could not open checkpoint file");
  }

  // 与 SimpleSim/save_checkpoint 写入格式保持一致，确保反序列化对齐。
  // 注意：reserve_* 当前在本模拟器中暂未使用，但必须读取以避免流错位。
  typedef struct Ckpt_CPU_state {
    uint32_t gpr[32];
    uint32_t csr[21];
    uint32_t pc;

    uint32_t store_addr;
    uint32_t store_data;
    uint32_t store_strb;
    bool store;
    bool reserve_valid;
    uint32_t reserve_addr;
  } Ckpt_CPU_state;

  Ckpt_CPU_state ckpt_state;
  uint64_t interval_inst_count;

  // 1. 恢复状态
  gz_read_pod(file, ckpt_state);
  gz_read_pod(file, interval_inst_count);

  CPU_state state;
  memcpy(state.gpr, ckpt_state.gpr, sizeof(state.gpr));
  memcpy(state.csr, ckpt_state.csr, sizeof(state.csr));
  state.pc = ckpt_state.pc;
  state.store_addr = ckpt_state.store_addr;
  state.store_data = ckpt_state.store_data;
  state.store_strb = ckpt_state.store_strb;
  state.store = ckpt_state.store;

  // 初始化新字段
  state.instruction = 0;
  state.page_fault_inst = false;
  state.page_fault_load = false;
  state.page_fault_store = false;
  state.inst_idx = 0;

  number_PC = state.pc;
  // 约束：当前 checkpoint 生成流程要求快照时处于 U 态，因此恢复时固定回到 U
  // 态。 若未来支持在 S/M 态打点，需要把特权级一并写入并按快照值恢复。
  csr->privilege = csr->privilege_1 = RISCV_MODE_U;

  for (int i = 0; i < ARF_NUM; i++) {
    prf->reg_file[i] = state.gpr[i];
    prf->reg_file_1[i] = state.gpr[i];
  }

  for (int i = 0; i < CSR_NUM; i++) {
    csr->CSR_RegFile[i] = state.csr[i];
    csr->CSR_RegFile_1[i] = state.csr[i];
  }

  // Populate global dut_cpu for Oracle state comparison
  memcpy(dut_cpu.gpr, state.gpr, sizeof(dut_cpu.gpr));
  memcpy(dut_cpu.csr, state.csr, sizeof(dut_cpu.csr));
  dut_cpu.pc = state.pc;
  dut_cpu.store = state.store;
  dut_cpu.store_addr = state.store_addr;
  dut_cpu.store_data = state.store_data;

  // 2. 恢复内存
  if (p_memory == nullptr) {
    Assert(0 && "Error: Memory not allocated during checkpoint restore.");
  }

  // [重要] 计算总字节数 (checkpoint 为 4GB)
  uint64_t total_bytes = 4ULL * 1024 * 1024 * 1024;
  uint8_t *byte_ptr = reinterpret_cast<uint8_t *>(p_memory);
  uint64_t remain = total_bytes;

  std::cout << "Restoring Memory..." << std::endl;

  const uint64_t GZ_CHUNK_SIZE = 1ULL * 1024 * 1024 * 1024;
  while (remain > 0) {
    unsigned int chunk = (remain > GZ_CHUNK_SIZE) ? (unsigned int)GZ_CHUNK_SIZE
                                                  : (unsigned int)remain;

    int read_bytes = gzread(file, byte_ptr, chunk);
    if (read_bytes < 0) {
      Assert(0 && "Error: gzread failed during checkpoint restore.");
    }
    if (read_bytes == 0) {
      Assert(0 && "Error: Unexpected EOF during checkpoint restore.");
    }

    byte_ptr += read_bytes;
    remain -= read_bytes;
  }

  gzclose(file);
  std::cout << "Checkpoint restored from " << final_name << std::endl;

  init_diff_ckpt(state, p_memory);
#ifndef CONFIG_BPU
  init_oracle_ckpt(state, p_memory, RISCV_MODE_U);
#endif

  // Ensure the pipeline starts with a refetch from the restored PC
  out.flush = true;
  out.redirect_pc = state.pc;
}
