#include "BPU/target_predictor/btb.h"
#include "ref.h"
#include <MMU.h>
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <front_IO.h>
#include <front_module.h>
#include <fstream>
#include <util.h>
#include "Dcache_Utils.h"
using namespace std;

#ifndef CONFIG_CACHE
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);
#else
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
          bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory, bool dut_flag = true);
#endif
void front_cycle(bool, bool, bool, uint32_t &);

// bpu 更新信息
void back2front_comb(front_top_in &front_in, front_top_out &front_out);
static inline void back2mmu_comb();

// 性能计数器
Perf_count perf;
Back_Top back;
MMU mmu;
front_top_out front_out;
front_top_in front_in;

long long sim_time = 0;
bool sim_end = false;

#ifndef CONFIG_CACHE
uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
#else
extern uint32_t *p_memory;
#endif
int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  ifstream inst_data(argv[argc - 1], ios::in | ios::binary);
  if (!inst_data.is_open()) {
    cout << "Error: Image " << argv[argc - 1] << " does not exist" << endl;
    exit(0);
  }

  inst_data.seekg(0, std::ios::end);
  streamsize size = inst_data.tellg();
  inst_data.seekg(0, std::ios::beg);

  if (!inst_data.read(reinterpret_cast<char *>(p_memory + 0x80000000 / 4),
                      size)) {
    std::cerr << "读取文件失败！" << std::endl;
    return 1;
  }

  inst_data.close();

  p_memory[uint32_t(0x0 / 4)] = 0xf1402573;
  p_memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  p_memory[uint32_t(0x8 / 4)] = 0x800002b7;
  p_memory[uint32_t(0xc / 4)] = 0x00028067;
  p_memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关

  printf("Load image size: %ld bytes\n", size);
  printf("Program entry point: 0x%08x\n",
         p_memory[uint32_t(0x80000000 / 4)]);
#ifdef CONFIG_DIFFTEST
  init_difftest(size);
#endif

#ifdef CONFIG_RUN_REF
  while (1) {
    difftest_step();
    sim_time++;
  }
#endif

  back.init();
  mmu.reset();

  uint32_t number_PC = 0;
  bool stall, misprediction, exception;
  stall = misprediction = exception = false;

#ifdef CONFIG_BPU
  // reset
  front_in.reset = true;
  front_in.FIFO_read_enable = true;
  front_top(&front_in, &front_out);
  cout << hex << front_out.pc[0] << endl;
  front_in.reset = false;
#endif

  // main loop
  for (sim_time = 0; sim_time < MAX_SIM_TIME; sim_time++) {
    if (sim_time % 10000000 == 0)
      cout << dec << sim_time << endl;
    perf.cycle++;

    if (LOG) {
      cout
          << "****************************************************************"
          << dec << " cycle: " << sim_time
          << " ****************************************************************"
          << endl;
    }

    back.comb_csr_status(); // 获取mstatus sstatus satp

#ifdef CONFIG_MMU
    // Backend(CSR) -> mmu
    back2mmu_comb();
    // step1: fetch instructions and fill in back.in
#endif

    front_cycle(stall, misprediction, exception, number_PC);

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
#ifdef CONFIG_BPU
    back2front_comb(front_in, front_out);
#endif

    back.seq();

#ifdef CONFIG_MMU
    mmu.seq();
#endif

    if (sim_end)
      break;

    stall = back.out.stall;
    misprediction = back.out.mispred;
    exception = back.out.flush;

    if (misprediction || exception) {
      number_PC = back.out.redirect_pc;
    } else if (stall) {
      for (int j = 0; j < FETCH_WIDTH; j++) {
        if (back.out.fire[j])
          back.in.valid[j] = false;
      }
    }
  }

SIM_END:

  delete[] p_memory;

  if (sim_time != MAX_SIM_TIME) {
    cout << "\033[1;32m-----------------------------\033[0m" << endl;
    cout << "\033[1;32mSuccess!!!!\033[0m" << endl;
    perf.perf_print();
    cout << "\033[1;32m-----------------------------\033[0m" << endl;

  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

  return 0;
}
#ifndef CONFIG_CACHE
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
    return true;
  }

  return false;
}
#else
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory, bool dut_flag) {
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
  if (DCACHE_LOG)
  {
    if (dut_flag)
      printf("DUT CPU: ");
    else
      printf("Ref CPU: ");
    printf("MMU va2pa v_addr:0x%08x satp:0x%08x pte1_addr:0x%08x\n", v_addr, satp, pte1_addr);
    if (dut_flag)
      printf("DUT CPU: ");
    else
      printf("Ref CPU: ");
  }
  bool pte1_in_cache =
      dcache_read(pte1_addr, pte1_entry_cache); 
  if (pte1_in_cache && dut_flag)
  {
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
if (DCACHE_LOG)
  {
    if (dut_flag)
      printf("DUT CPU: ");
    else
      printf("Ref CPU: ");
    printf("MMU va2pa v_addr:0x%08x satp:0x%08x pte2_addr:0x%08x\n", v_addr, satp, pte2_addr);
    if (dut_flag)
      printf("DUT CPU: ");
    else
      printf("Ref CPU: ");
  }
  uint32_t pte2_entry_cache;
  bool pte2_in_cache =
      dcache_read(pte2_addr, pte2_entry_cache); 
  if (pte2_in_cache && dut_flag)
  {
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
    if(DCACHE_LOG){
        if (dut_flag)
          printf("DUT CPU: ");
        else
          printf("Ref CPU: ");
        printf("MMU va2pa success v_addr:0x%08x p_addr:0x%08x\n", v_addr, p_addr);
    }
    return true;
  }

  return false;
}
#endif
/*
 * va2pa_fixed: a fixed version of va2pa
 *
 * 基本功能几乎与 va2pa() 相同；但当 dut.cpu 检测到 page fault 时，
 * 以 dut.cpu 的结果为准，
 *
 * 目的：当 SFENCE.VMA 还没有执行、存在两种合法的页表映射时，保证
 * DUT 与参考模型的页表映射一致，避免 difftest 失败。
 */
bool va2pa_fixed(uint32_t &p_addr, uint32_t v_addr, uint32_t satp,
                 uint32_t type, bool *mstatus, bool *sstatus, int privilege,
                 uint32_t *p_memory) {
  bool ret =
      va2pa(p_addr, v_addr, satp, type, mstatus, sstatus, privilege, p_memory);
#ifndef CONFIG_LOOSE_VA2PA
  return ret;
#endif
  extern int ren_commit_idx; // extern from Rename.cpp, for difftest debug
  Inst_entry ren_commit_entry = back.out.commit_entry[ren_commit_idx];
  bool dut_page_fault_inst = ren_commit_entry.uop.page_fault_inst;
  bool dut_page_fault_load = ren_commit_entry.uop.page_fault_load;
  bool dut_page_fault_store = ren_commit_entry.uop.page_fault_store;

  // 1. dut page_fault, ref no page_fault -> allow, ret = false
  // 2. dut no page_fault, ref page_fault -> ERROR
  switch (type) {
  case 0: // instruction fetch
    if (dut_page_fault_inst) {
      ret = false; // 以 DUT MMU 为准
    } else if (!dut_page_fault_inst && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed instruction fetch page fault "
              "mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  case 1: // load
    if (dut_page_fault_load) {
      ret = false;
    } else if (!dut_page_fault_load && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed load page fault mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  case 2: // store
    if (dut_page_fault_store) {
      ret = false;
    } else if (!dut_page_fault_store && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed store page fault mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  default:
    cout << "[va2pa_fixed] Error: unknown access type!" << endl;
    exit(1);
  }
  return ret;
}

void front_cycle(bool stall, bool misprediction, bool exception,
                 uint32_t &number_PC) {

  if (!stall || misprediction || exception) {

#if defined(CONFIG_BPU)

    front_in.FIFO_read_enable = true;
    front_in.refetch = (misprediction || exception);
    front_top(&front_in, &front_out);

#else
    for (int j = 0; j < FETCH_WIDTH; j++) {
      front_out.pc[j] = number_PC;
      front_out.FIFO_valid = true;
      front_out.inst_valid[j] = true;

      uint32_t p_addr;

      bool mstatus[32], sstatus[32];

      cvt_number_to_bit_unsigned(mstatus, back.out.mstatus, 32);

      cvt_number_to_bit_unsigned(sstatus, back.out.sstatus, 32);

      if ((back.out.satp & 0x80000000) && back.out.privilege != 3) {

        front_out.page_fault_inst[j] =
            !va2pa(p_addr, number_PC, back.out.satp, 0, mstatus, sstatus,
                   back.out.privilege, p_memory);
        if (front_out.page_fault_inst[j]) {
          front_out.instructions[j] = INST_NOP;
        } else {
          front_out.instructions[j] = p_memory[p_addr / 4];
        }
      } else {
        front_out.page_fault_inst[j] = false;
        front_out.instructions[j] = p_memory[number_PC / 4];
      }

      front_out.predict_dir[j] = false;
      number_PC += 4;
    }

    front_out.predict_next_fetch_address = number_PC;

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

void back2front_comb(front_top_in &front_in, front_top_out &front_out) {
  front_in.FIFO_read_enable = false;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    Inst_uop *inst = &back.out.commit_entry[i].uop;
    front_in.back2front_valid[i] = back.out.commit_entry[i].valid;
    // front_in.back2front_valid[i] = back.out.commit_entry[i].valid &&
    //                                (is_branch(inst->type) || inst->type ==
    //                                JAL);
    if (front_in.back2front_valid[i]) {
      front_in.predict_dir[i] = inst->pred_br_taken;
      front_in.predict_base_pc[i] = inst->pc;
      front_in.actual_dir[i] =
          (inst->type == JAL || inst->type == JALR) ? true : inst->br_taken;
      front_in.actual_target[i] = inst->pc_next;
      int br_type = BR_DIRECT;
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

static inline void back2mmu_comb() {
  mmu.io.in.state.satp = reinterpret_cast<satp_t &>(back.out.satp);
  mmu.io.in.state.mstatus = back.out.mstatus;
  mmu.io.in.state.sstatus = back.out.sstatus;
  mmu.io.in.state.privilege = mmu_n::Privilege(back.out.privilege);
  // for flush tlb:
  // - if request flush, set flush_valid = true in back-end later
  mmu.io.in.tlb_flush.flush_valid = false;

  mmu.io.in.mmu_dcache_req = *back.out.dcache2ptw_req;
  mmu.io.in.mmu_dcache_resp = *back.out.dcache2ptw_resp;

  back.in.ptw2dcache_req = &mmu.io.out.mmu_dcache_req;
  back.in.ptw2dcache_resp = &mmu.io.out.mmu_dcache_resp;


}
