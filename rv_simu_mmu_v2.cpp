#include "BPU/target_predictor/btb.h"
#include "CSR.h"
#include "frontend.h"
#include "ref.h"
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

using namespace std;

// 性能计数器
Perf_count perf;

bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);
void front_cycle(bool, bool, bool, front_top_in &, front_top_out &, uint32_t &,
                 bool &);

// bpu 更新信息
void back2front_comb(front_top_in &front_in, front_top_out &front_out);

Back_Top back;
long long sim_time = 0;
bool sim_end = false;

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  ifstream inst_data(argv[argc - 1], ios::in);

  if (!inst_data.is_open()) {
    cout << "Error: Image " << argv[argc - 1] << " does not exist" << endl;
    exit(0);
  }

  inst_data.seekg(0, std::ios::end);
  streamsize size = inst_data.tellg();
  inst_data.seekg(0, std::ios::beg);

  // 读取数据到 vector<char>
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

  uint32_t number_PC;
  ofstream outfile;
  bool stall, misprediction, exception;
  number_PC = 0x00000000;
  stall = misprediction = exception = false;
  bool non_branch_mispred = false;

  front_top_out front_out;
  front_top_in front_in;

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

    if (LOG)
      cout
          << "****************************************************************"
          << dec << " cycle: " << sim_time
          << " ****************************************************************"
          << endl;

    // step1: fetch instructions and fill in back.in

    front_cycle(stall, misprediction, exception, front_in, front_out, number_PC,
                non_branch_mispred);

    back.Back_comb();

    // step2: feedback to front-end
#ifdef CONFIG_BPU
    back2front_comb(front_in, front_out);
#endif

    back.Back_seq();

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
    cout << endl;

  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

  return 0;
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

bool load_data(uint32_t &data, uint32_t v_addr, int rob_idx) {
  uint32_t p_addr = v_addr;
  bool ret = true;

  if (back.csr.CSR_RegFile[csr_satp] & 0x80000000 && back.csr.privilege != 3) {
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, back.csr.CSR_RegFile[csr_mstatus], 32);

    cvt_number_to_bit_unsigned(sstatus, back.csr.CSR_RegFile[csr_sstatus], 32);

    ret = va2pa(p_addr, v_addr, back.csr.CSR_RegFile[csr_satp], 1, mstatus,
                sstatus, back.csr.privilege, p_memory);
  }

  if (p_addr == 0x1fd0e000) {
    data = perf.commit_num;
  } else if (p_addr == 0x1fd0e004) {
    data = 0;
  } else {
    data = p_memory[p_addr >> 2];
    back.stq.st2ld_fwd(p_addr, data, rob_idx);
  }

  return ret;
}

void front_cycle(bool stall, bool misprediction, bool exception,
                 front_top_in &front_in, front_top_out &front_out,
                 uint32_t &number_PC, bool &non_branch_mispred) {
  if (!stall || misprediction || exception) {

#if defined(CONFIG_BPU)

    front_in.FIFO_read_enable = true;
    front_in.refetch = (misprediction || exception || non_branch_mispred);
    // for (int i = 0; i < COMMIT_WIDTH; i++) {
    //   if (front_in.back2front_valid[i]) {
    //     Inst_entry *inst = &back.rob.io.rob_commit->commit_entry[i];
    //     if (!(inst->valid && is_branch(inst->uop.type) &&
    //           front_in.predict_base_pc[i] == inst->uop.pc)) {
    //       cout << "error" << endl;
    //     }
    //   }
    // }
    front_top(&front_in, &front_out);

#else
    for (int j = 0; j < FETCH_WIDTH; j++) {
      front_out.pc[j] = number_PC;
      front_out.FIFO_valid = true;
      front_out.inst_valid[j] = true;

      uint32_t p_addr;

      bool mstatus[32], sstatus[32];

      cvt_number_to_bit_unsigned(mstatus, back.csr.CSR_RegFile[csr_mstatus],
                                 32);

      cvt_number_to_bit_unsigned(sstatus, back.csr.CSR_RegFile[csr_sstatus],
                                 32);

      if ((back.csr.CSR_RegFile[csr_satp] & 0x80000000) &&
          back.csr.privilege != 3) {

        front_out.page_fault_inst[j] =
            !va2pa(p_addr, number_PC, back.csr.CSR_RegFile[csr_satp], 0,
                   mstatus, sstatus, back.csr.privilege, p_memory);
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
    bool is_br = false;
    bool is_jal = false;
    bool is_jalr = false;
    uint32_t last_valid_inst_pc = 0;
    for (int j = 0; j < FETCH_WIDTH; j++) {
      back.in.valid[j] =
          front_out.FIFO_valid && no_taken && front_out.inst_valid[j];
      if (back.in.valid[j])
        last_valid_inst_pc = front_out.pc[j];
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

      // pre-decode, avoid non-branch instruction be predicted as taken
      // 直接处理jal的跳转
      // 判断条件跳转的地址是否正确
      // 判断jalr是否被正确预测为跳转

      uint32_t op_branch = 0b1100011;
      uint32_t op_jal = 0b1101111;
      uint32_t op_jalr = 0b1100111;
      uint32_t number_op_code_unsigned = front_out.instructions[j] & 0x7f;
      is_jal = (number_op_code_unsigned == op_jal);
      is_jalr = (number_op_code_unsigned == op_jalr);
      is_br = (number_op_code_unsigned == op_branch);
      bool is_branch_inst = is_br || is_jal || is_jalr;
      if (front_out.predict_dir[j] && is_branch_inst)
        no_taken = false;

      // 预解码处理
      if (back.in.valid[j]) {

        if (is_jalr) {
          // 判断jalr是否被正确预测为跳转
        } else if (is_br) {
          // 检查预测跳转的地址是否正确
        } else if (is_jal) {
          // 直接处理jal的跳转
        }
      }
    }

    non_branch_mispred = false;
    if (no_taken &&
        (front_out.predict_next_fetch_address != last_valid_inst_pc + 4) &&
        front_out.FIFO_valid) {
      // 发生了非分支指令的错误预测
      non_branch_mispred = true;
      front_in.refetch = true;
      front_in.refetch_address = last_valid_inst_pc + 4;
    }
  } else {
#ifdef CONFIG_BPU
    /*
     * stall && !misprediction && !exception
     */
    front_in.FIFO_read_enable = false;
    front_in.refetch = false;
    front_in.refetch = non_branch_mispred;
    // for (int i = 0; i < COMMIT_WIDTH; i++) {
    //   if (front_in.back2front_valid[i]) {
    //     Inst_entry *inst = &back.rob.io.rob_commit->commit_entry[i];
    //     if (!(inst->valid && is_branch(inst->uop.type) &&
    //           front_in.predict_base_pc[i] == inst->uop.pc)) {
    //       cout << "error" << endl;
    //     }
    //   }
    // }

    front_top(&front_in, &front_out);
    non_branch_mispred = false;
#endif
  }
}

void back2front_comb(front_top_in &front_in, front_top_out &front_out) {
  front_in.FIFO_read_enable = false;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    Inst_uop *inst = &back.out.commit_entry[i].uop;
    front_in.back2front_valid[i] = back.out.commit_entry[i].valid;
    // is_branch(back.out.commit_entry[i].uop.type);

    if (front_in.back2front_valid[i]) {
      front_in.predict_dir[i] = inst->pred_br_taken;
      front_in.predict_base_pc[i] = inst->pc;
      front_in.actual_dir[i] = (is_branch(inst->type)) ? inst->br_taken : false;
      // front_in.actual_dir[i] = inst->br_taken;
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
    /*cout << " valid: " << front_in.back2front_valid[i]*/
    /*     << " 反馈给前端的分支指令PC: " << hex << inst->pc*/
    /*     << " 预测结果: " << inst->pred_br_taken*/
    /*     << " 实际结果: " << inst->br_taken*/
    /*     << " 预测目标地址: " << inst->pred_br_pc*/
    /*     << " 实际目标地址: " << inst->pc_next*/
    /*     << " 指令: " << inst->instruction << endl;*/
    // }
  }
  // if (LOG) {
  // show ROB valid vector
  /*cout << "ROB count: " << back.rob.count << " deq_ptr: " <<
   * back.rob.deq_ptr*/
  /*     << " enq_ptr: " << back.rob.enq_ptr << endl;*/
  /*cout << "ROB valid: \n";*/
  /*for (int i = 0; i < ROB_NUM; i++) {*/
  /*  cout << back.rob.entry[i].valid << " ";*/
  /*  if (i % 32 == 31)*/
  /*    cout << endl;*/
  /*}*/
  /*cout << "ROB inst pc/inst:" << endl;*/
  /*for (int i = 0; i < ROB_NUM; i++) {*/
  /*  if (back.rob.entry[i].valid) {*/
  /*    cout << hex << back.rob.entry[i].uop.pc << " at " << hex << i*/
  /*         << " , inst: " << back.rob.entry[i].uop.instruction*/
  /*         << " , decode at sim_time: " << dec*/
  /*         << back.rob.entry[i].uop.inst_idx*/
  /*         << " , complete: " << back.rob.complete[i]*/
  /*         << " , exception: " << back.rob.exception[i] << endl;*/
  /*  }*/
  /*}*/
  // }

  // if (back.out.mispred) {
  if (back.out.mispred || back.out.flush) {
    // 若 pre-decode 或后端都检测到 branch mis-prediction，则以后端为准
    front_in.refetch_address = back.out.redirect_pc;
  }
}
