#include "BackTop.h"
#include "Csr.h"
#include "IO.h"
#include "PhysMemory.h"
#include "RealLsu.h"
#include "config.h"
#include "diff.h"
#include "oracle.h"
#include "ref.h"
#include "util.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <zlib.h>

void init_diff_ckpt(CPU_state ckpt_state);

void BackTop::init() {
  pre = new PreIduQueue(ctx);
  idu = new Idu(ctx, MAX_BR_PER_CYCLE);
  rename = new Ren(ctx);
  dis = new Dispatch(ctx);
  isu = new Isu(ctx);
  prf = new Prf(ctx);
  exu = new Exu(ctx);
  csr = new Csr();
  rob = new Rob(ctx);
  lsu = new RealLsu(ctx);
  
  out.fire = pre2front.fire;

  pre->out.pre2front = &pre2front;
  pre->out.issue = &pre_issue;
  pre->out.ftq_exu_pc_resp = &ftq_exu_pc_resp;
  pre->out.ftq_rob_pc_resp = &ftq_rob_pc_resp;
  pre->in.front2pre = &in;
  pre->in.idu_consume = &idu_consume;
  pre->in.rob_bcast = &rob_bcast;
  pre->in.rob_commit = &rob_commit;
  pre->in.idu_br_latch = &idu->br_latch;
  pre->in.ftq_exu_pc_req = &ftq_exu_pc_req;
  pre->in.ftq_rob_pc_req = &ftq_rob_pc_req;

  idu->out.dec2ren = &dec2ren;
  idu->out.dec_bcast = &dec_bcast;
  idu->out.idu_consume = &idu_consume;
  idu->in.issue = &pre_issue;
  idu->in.ren2dec = &ren2dec;
  idu->in.rob_bcast = &rob_bcast;
  idu->in.exu2id = &exu2id;

  rename->in.dec2ren = &dec2ren;
  rename->in.dis2ren = &dis2ren;
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
  exu->in.ftq_pc_resp = &ftq_exu_pc_resp;

  exu->out.exe2prf = &exe2prf;
  exu->out.exe2iss = &exe2iss;
  exu->out.exe2lsu = &exe2lsu;
  exu->out.exe2lsu = &exe2lsu;
  exu->out.exe2csr = &exe2csr;
  exu->out.exu2id = &exu2id;
  exu->out.exu2rob = &exu2rob;
  exu->out.ftq_pc_req = &ftq_exu_pc_req;

  rob->in.dis2rob = &dis2rob;
  rob->in.dec_bcast = &dec_bcast;
  rob->in.lsu2rob = &lsu2rob;
  rob->in.csr2rob = &csr2rob;
  rob->in.exu2rob = &exu2rob;
  rob->in.ftq_pc_resp = &ftq_rob_pc_resp;

  rob->out.rob_bcast = &rob_bcast;
  rob->out.rob_commit = &rob_commit;
  rob->out.rob2dis = &rob2dis;
  rob->out.rob2csr = &rob2csr;
  rob->out.ftq_pc_req = &ftq_rob_pc_req;

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
  lsu->in.peripheral_resp = &peripheral_resp_io;
  lsu->in.dcache2lsu  = &dcache2lsu_io;

  lsu->out.lsu2exe = &lsu2exe;
  lsu->out.lsu2dis = &lsu2dis;
  lsu->out.lsu2rob = &lsu2rob;
  lsu->out.peripheral_req = &peripheral_req_io;
  lsu->out.lsu2dcache = &lsu2dcache_io;

  pre->init();
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
  pre2front = {};
  pre_issue = {};
  ftq_exu_pc_req = {};
  ftq_exu_pc_resp = {};
  ftq_rob_pc_req = {};
  ftq_rob_pc_resp = {};

  dec2ren = {};
  dec_bcast = {};

  ren2dec = {};
  idu_consume = {};
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
  peripheral_req_io = {};
  peripheral_resp_io = {};
  lsu2dcache_io = {};
#endif

  pre->comb_begin();
  idu->comb_begin();
  rename->comb_begin();
  dis->comb_begin();
  isu->comb_begin();
  prf->comb_begin();
  exu->comb_begin();
  rob->comb_begin();
  csr->comb_begin();

  // 每个空行表示分层  下层会依赖上层产生的某个信号
  pre->comb_accept_front();
  idu->comb_decode();
  csr->comb_interrupt();
  rename->comb_alloc();
  prf->comb_complete();
  prf->comb_awake();
  prf->comb_write();
  isu->comb_ready();
  lsu->comb_cal();
  lsu->comb_lsu2dis_info();

  idu->comb_branch();

  rob->comb_ready();
  rob->comb_ftq_pc_req();
  exu->comb_ftq_pc_req();
  pre->comb_ftq_lookup();
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
  dis->comb_wake();
  rename->comb_rename();

  dis->comb_dispatch();

  // 用于调试
  // 修正pc_next 以及difftest对应的pc_next
  out.flush = rob->out.rob_bcast->flush;
  out.fence_i = rob->out.rob_bcast->fence_i;
  out.itlb_flush = rob->out.rob_bcast->fence;

  // 1. Normal case (No Rob flush)
  if (!rob->out.rob_bcast->flush) {
    out.mispred = idu->out.dec_bcast->mispred;
    out.stall = !pre2front.ready;
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
    out.commit_entry[i] =
        rob->out.rob_commit->commit_entry[i].uop.to_inst_entry(
            rob->out.rob_commit->commit_entry[i].valid);
    if (out.commit_entry[i].valid && out.flush) {
      // Flush: override extra_data.pc_next with redirect_pc for ALL
      // instructions so Difftest can read the correct next-PC (trap vector,
      // epc, etc.)
      out.commit_entry[i].uop.diag_val = out.redirect_pc;
      rob->out.rob_commit->commit_entry[i].uop.diag_val = out.redirect_pc;
    }
  }

  dis->comb_fire();
  rename->comb_fire();
  idu->comb_fire();

  isu->comb_enq();
  // Rob recovery was split out of comb_fire(); keep the original priority:
  // branch rollback happens before enqueue, and global flush overrides both.
  rob->comb_branch();
  rob->comb_fire();
  rob->comb_flush();
  isu->comb_flush();
  lsu->comb_flush();
  pre->comb_fire();
  prf->comb_pipeline();
  exu->comb_pipeline();
  dis->comb_pipeline();
  rename->comb_pipeline();
}

void BackTop::seq() {
  // rename -> isu/stq/rob
  // exu -> prf
  pre->seq();
  rename->seq();
  dis->seq();
  idu->seq();
  isu->seq();
  exu->seq();
  prf->seq();
  rob->seq();
  csr->seq();
  lsu->seq();
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

namespace {
constexpr uint64_t kCkptSimpointRamBytes = RAM_SIZE;
constexpr uint64_t kGzChunkSize = 1ULL * 1024 * 1024 * 1024;
constexpr uint32_t kCkptMagic = 0x006d6552u; // "Rem\0" little-endian
constexpr uint32_t kCkptVersion = 2u;

typedef struct CkptHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t ram_size;
  uint32_t io_range_count;
} CkptHeader;

typedef struct CkptIoRange {
  uint32_t base;
  uint32_t size;
} CkptIoRange;

constexpr std::array<CkptIoRange, 4> kExpectedIoLayout = {
    CkptIoRange{BOOT_IO_BASE, BOOT_IO_SIZE},
    CkptIoRange{UART_ADDR_BASE, UART_MMIO_SIZE},
    CkptIoRange{PLIC_ADDR_BASE, PLIC_MMIO_SIZE},
    CkptIoRange{OPENSBI_TIMER_BASE, OPENSBI_TIMER_MMIO_SIZE},
};

void gz_read_exact(gzFile file, uint8_t *dst, uint64_t total_bytes) {
  uint64_t remain = total_bytes;
  while (remain > 0) {
    const unsigned int chunk = static_cast<unsigned int>(
        remain > kGzChunkSize ? kGzChunkSize : remain);
    const int read_bytes = gzread(file, dst, chunk);
    if (read_bytes < 0) {
      Assert(0 && "Error: gzread failed during checkpoint restore.");
    }
    if (read_bytes == 0) {
      Assert(0 && "Error: Unexpected EOF during checkpoint restore.");
    }
    dst += read_bytes;
    remain -= static_cast<uint64_t>(read_bytes);
  }
}

} // namespace

void BackTop::load_image(const std::string &filename) {
  std::ifstream inst_data(filename, std::ios::binary);
  if (!inst_data.is_open()) {
    Assert(0 && "Error: Image does not exist");
  }

  inst_data.seekg(0, std::ios::end);
  std::streamsize size = inst_data.tellg();
  inst_data.seekg(0, std::ios::beg);

  if (size < 0 || static_cast<uint64_t>(size) > RAM_SIZE) {
    Assert(0 && "Image too large for configured RAM window.");
  }
  if (!inst_data.read(reinterpret_cast<char *>(pmem_ram_ptr()), size)) {
    Assert(0 && "读取文件失败！");
  }

  inst_data.close();

  pmem_write(0x0, 0xf1402573);
  pmem_write(0x4, 0x83e005b7);
  pmem_write(0x8, 0x800002b7);
  pmem_write(0xc, 0x00028067);
  pmem_write(0x10000004, 0x00006000); // 和进入 OpenSBI 相关

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
  get_state(state, privilege);
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
  dut_cpu.store_strb = state.store_strb;
  dut_cpu.reserve_valid = state.reserve_valid;
  dut_cpu.reserve_addr = state.reserve_addr;
  lsu->restore_reservation(state.reserve_valid, state.reserve_addr);

  // Ensure the pipeline starts with a refetch from the restored PC
  out.flush = true;
  out.redirect_pc = state.pc;

#ifndef CONFIG_BPU
  // FAST 模式从 ref 切换到 O3+oracle 时，oracle 也必须恢复到同一状态，
  // 否则首拍 refetch 会因为 PC 不一致触发断言。
  init_oracle_ckpt(state, privilege);
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
  CkptHeader ckpt_header = {};
  uint64_t interval_inst_count;

  // 1. 恢复 header + 状态
  gz_read_pod(file, ckpt_header);
  Assert(ckpt_header.magic == kCkptMagic &&
         "Error: Invalid checkpoint magic.");
  Assert(ckpt_header.version == kCkptVersion &&
         "Error: Unsupported checkpoint version.");
  Assert(ckpt_header.ram_size == static_cast<uint32_t>(kCkptSimpointRamBytes) &&
         "Error: Checkpoint RAM size mismatch.");
  Assert(ckpt_header.io_range_count == kExpectedIoLayout.size() &&
         "Error: Checkpoint IO layout count mismatch.");

  gz_read_pod(file, ckpt_state);
  gz_read_pod(file, interval_inst_count);
  ckpt_interval_inst_count = interval_inst_count;

  CPU_state state;
  memcpy(state.gpr, ckpt_state.gpr, sizeof(state.gpr));
  memcpy(state.csr, ckpt_state.csr, sizeof(state.csr));
  state.pc = ckpt_state.pc;
  state.store_addr = ckpt_state.store_addr;
  state.store_data = ckpt_state.store_data;
  state.store_strb = ckpt_state.store_strb;
  state.store = ckpt_state.store;
  state.reserve_valid = ckpt_state.reserve_valid;
  state.reserve_addr = ckpt_state.reserve_addr;

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
  dut_cpu.store_strb = state.store_strb;
  dut_cpu.reserve_valid = state.reserve_valid;
  dut_cpu.reserve_addr = state.reserve_addr;
  lsu->restore_reservation(state.reserve_valid, state.reserve_addr);

  // 2. 恢复内存
  if (pmem_ram_ptr() == nullptr) {
    Assert(0 && "Error: Memory not allocated during checkpoint restore.");
  }
  pmem_clear_all();

  std::cout << "Restoring Memory... format=simpoint-v2(header+ranges)"
            << std::endl;

  uint8_t *ram_dst = reinterpret_cast<uint8_t *>(pmem_ram_ptr());
  gz_read_exact(file, ram_dst, kCkptSimpointRamBytes);

  // 3. 恢复并校验 IO 布局（range descriptor + raw bytes）
  for (uint32_t i = 0; i < ckpt_header.io_range_count; ++i) {
    CkptIoRange range = {};
    gz_read_pod(file, range);
    const auto &expected = kExpectedIoLayout[i];
    Assert(range.base == expected.base && range.size == expected.size &&
           "Error: Checkpoint IO layout mismatch.");

    std::vector<uint8_t> io_bytes(range.size, 0);
    gz_read_exact(file, io_bytes.data(), io_bytes.size());
    for (uint32_t off = 0; off + 4 <= range.size; off += 4) {
      const uint32_t word =
          static_cast<uint32_t>(io_bytes[off + 0]) |
          (static_cast<uint32_t>(io_bytes[off + 1]) << 8) |
          (static_cast<uint32_t>(io_bytes[off + 2]) << 16) |
          (static_cast<uint32_t>(io_bytes[off + 3]) << 24);
      if (word != 0) {
        pmem_write(range.base + off, word);
      }
    }
  }

  gzclose(file);
  std::cout << "Checkpoint restored from " << final_name << std::endl;

  init_diff_ckpt(state);
#ifndef CONFIG_BPU
  init_oracle_ckpt(state, RISCV_MODE_U);
#endif

  // Ensure the pipeline starts with a refetch from the restored PC
  out.flush = true;
  out.redirect_pc = state.pc;
}
