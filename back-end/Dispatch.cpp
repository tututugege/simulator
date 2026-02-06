#include "config.h"
#include <Dispatch.h>
#include <SimCpu.h>
#include <util.h>

void Dispatch::comb_alloc() {
  int store_alloc_count = 0; // 当前周期已分配的 store 数量
  wire<16> current_cycle_store_mask = 0;

  // 初始化输出
  for (int k = 0; k < MAX_STQ_DISPATCH_WIDTH; k++) {
    out.dis2lsu->alloc_req[k] = false;
    stq_port_mask[k] = 0;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_alloc[i] = inst_r[i];
    out.dis2rob->valid[i] = inst_r[i].valid;

    // 分配 ROB ID (重排序缓存索引)
    inst_alloc[i].uop.rob_idx = make_rob_idx(in.rob2dis->enq_idx, i);
    inst_alloc[i].uop.rob_flag = in.rob2dis->rob_flag;
    inst_alloc[i].uop.cplt_num = 0;  // 初始化完成计数器

    // Load 需要知道之前的 Store
    if (is_load(inst_r[i].uop)) {
      inst_alloc[i].uop.pre_sta_mask = current_cycle_store_mask;
    }

    // 处理 Store 分配
    if (inst_r[i].valid && is_store(inst_r[i].uop)) {
      // 检查是否有足够的 STQ 端口
      if (store_alloc_count < in.lsu2dis->stq_free) {
        // 计算 STQ Index
        int allocated_idx =
            (in.lsu2dis->stq_tail + store_alloc_count) % STQ_NUM;
        inst_alloc[i].uop.stq_idx = allocated_idx;

        // 填充 STQ 请求
        out.dis2lsu->tag[store_alloc_count] = inst_r[i].uop.tag;

        // 记录 Mask
        current_cycle_store_mask |= (1 << allocated_idx);
        stq_port_mask[store_alloc_count] =
            (1 << i); // 记录这条指令占用了这个端口

        store_alloc_count++;
      } else {
        out.dis2rob->valid[i] = false;
      }
    }

    out.dis2rob->uop[i] = inst_alloc[i].uop;
  }
}

// BusyTable 旁路 (BusyTable Bypass)
void Dispatch::comb_wake() {
  if (in.prf_awake->wake.valid) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (inst_alloc[i].uop.src1_preg == in.prf_awake->wake.preg) {
        inst_alloc[i].uop.src1_busy = false;
        inst_r_1[i].uop.src1_busy = false;
      }
      if (inst_alloc[i].uop.src2_preg == in.prf_awake->wake.preg) {
        inst_alloc[i].uop.src2_busy = false;
        inst_r_1[i].uop.src2_busy = false;
      }
    }
  }

  for (int i = 0; i < ALU_NUM + 2; i++) {
    if (in.iss_awake->wake[i].valid) {
      for (int j = 0; j < FETCH_WIDTH; j++) {
        if (inst_alloc[j].uop.src1_preg == in.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src1_busy = false;
          inst_r_1[j].uop.src1_busy = false;
        }
        if (inst_alloc[j].uop.src2_preg == in.iss_awake->wake[i].preg) {
          inst_alloc[j].uop.src2_busy = false;
          inst_r_1[j].uop.src2_busy = false;
        }
      }
    }
  }
}

void Dispatch::comb_dispatch() {
  // 1. 清空输出 req
  for (int i = 0; i < IQ_NUM; i++) {
    for (int w = 0; w < MAX_IQ_DISPATCH_WIDTH; w++) {
      out.dis2iss->req[i][w].valid = false;
    }
  }

  int iq_usage[IQ_NUM] = {0};

  for (int i = 0; i < FETCH_WIDTH; i++) {
    dispatch_success_flags[i] = false;
    dispatch_cache[i].count = 0; // 重置计数

    if (!inst_r[i].valid) {
      dispatch_success_flags[i] = true;
      continue;
    }

    // === 1. 临时拆分 (Full Data) ===
    // 在栈上分配，用完即弃，不占用类成员空间
    UopPacket temp_uops[MAX_UOPS_PER_INST];
    int cnt = decompose_inst(inst_alloc[i], temp_uops);

    // === 2. 相当于中间变量 存入缓存 ===
    dispatch_cache[i].count = cnt;
    for (int k = 0; k < cnt; k++) {
      dispatch_cache[i].iq_ids[k] = temp_uops[k].iq_id;
    }

    // === 3. 检查容量 ===

    bool fit = true;
    for (int k = 0; k < cnt; k++) {
      int target = temp_uops[k].iq_id;

      // ✅ 直接查表！不需要 Isu 告诉它，它自己就能看 config.h
      int port_limit = GLOBAL_IQ_CONFIG[target].dispatch_width;

      if (iq_usage[target] >= port_limit ||
          iq_usage[target] >= in.iss2dis->ready_num[target]) {
        fit = false;
        break;
      }
    }

    // === 4. 提交发射请求 ===
    if (fit) {
      dispatch_success_flags[i] = true;
      for (int k = 0; k < cnt; k++) {
        int target = temp_uops[k].iq_id;
        int slot = iq_usage[target];

        out.dis2iss->req[target][slot].valid = true;
        out.dis2iss->req[target][slot].uop = temp_uops[k].uop;

        iq_usage[target]++;
      }
    } else {
      break;
    }
  }
}

void Dispatch::comb_fire() {
  bool pre_stall = false;
  bool pre_fire = false;
  bool global_flush =
      in.rob_bcast->flush || in.dec_bcast->mispred || in.rob2dis->stall;

  // === 步骤 1: 计算 Fire 信号 (确认分派) ===
  for (int i = 0; i < FETCH_WIDTH; i++) {
    // 修正：有效的 Fire 信号意味着指令本身有效且资源分配（如 STQ）成功
    bool basic_fire = out.dis2rob->valid[i] &&
                      dispatch_success_flags[i] && // IQ 检查通过
                      in.rob2dis->ready &&         // ROB 有空间
                      !pre_stall && !global_flush;

    // 特殊检查：CSR
    if (is_CSR(inst_r[i].uop.type)) {
      if (!in.rob2dis->empty || pre_fire)
        basic_fire = false;
    }

    out.dis2rob->dis_fire[i] = basic_fire;

    if (inst_r[i].valid && !basic_fire)
      pre_stall = true;

    if (basic_fire)
      pre_fire = true;
  }

  // 更新 Rename 单元的 Ready 信号
  out.dis2ren->ready = !pre_stall;

  // === 步骤 2: 撤销无效的 IQ 请求 (回滚) ===
  int iq_slot_idx[IQ_NUM] = {0};

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (!inst_r[i].valid)
      continue;
    if (!dispatch_success_flags[i])
      break;

    // 从缓存读取元数据
    int cnt = dispatch_cache[i].count;

    if (!out.dis2rob->dis_fire[i]) {
      // Fire 失败 -> 撤销请求
      for (int k = 0; k < cnt; k++) {
        // 直接使用缓存的 ID
        int target = dispatch_cache[i].iq_ids[k];
        int slot = iq_slot_idx[target];

        out.dis2iss->req[target][slot].valid = false; // 撤销！
        iq_slot_idx[target]++;
      }
    } else {
      // Fire 成功 -> 跳过
      for (int k = 0; k < cnt; k++) {
        int target = dispatch_cache[i].iq_ids[k];
        iq_slot_idx[target]++;
      }
    }
  }

  // === 步骤 3: 更新 STQ 的 Fire 信号 ===
  for (int k = 0; k < MAX_STQ_DISPATCH_WIDTH; k++) {
    // 如果端口 k 分配给了指令 i，且指令 i Fire 了，则 STQ Fire
    if (stq_port_mask[k] != 0) {
      int inst_idx = __builtin_ctz(stq_port_mask[k]); // 找到对应的指令索引
      if (out.dis2rob->dis_fire[inst_idx]) {
        out.dis2lsu->alloc_req[k] = true;
        out.dis2lsu->tag[k] = out.dis2rob->uop[inst_idx].tag;
        out.dis2lsu->rob_idx[k] = out.dis2rob->uop[inst_idx].rob_idx;
        out.dis2lsu->rob_flag[k] = out.dis2rob->uop[inst_idx].rob_flag;
        out.dis2lsu->func3[k] = out.dis2rob->uop[inst_idx].func3;
      }
    }
  }
}

void Dispatch::comb_pipeline() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (in.rob_bcast->flush || in.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (out.dis2ren->ready) {
      inst_r_1[i].uop = in.ren2dis->uop[i];
      inst_r_1[i].valid = in.ren2dis->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !out.dis2rob->dis_fire[i];
    }
  }
}

void Dispatch::seq() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }
}

int Dispatch::decompose_inst(const InstEntry &inst, UopPacket *out_uops) {
  int count = 0;
  const auto &src_uop = inst.uop;

  switch (src_uop.type) {
  case ADD:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_ADD;
    count = 1;
    break;
  case MUL:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_MUL;
    count = 1;
    break;
  case DIV:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_DIV;
    count = 1;
    break;
  case BR:
    out_uops[0].iq_id = IQ_BR;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_BR;
    count = 1;
    break;

  case LOAD:
    out_uops[0].iq_id = IQ_LD;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_LOAD;
    count = 1;
    break;

  case STORE:
    // 拆分为 STA + STD
    out_uops[0].iq_id = IQ_STA;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_STA;
    out_uops[0].uop.src2_en = false; // STA 只用 src1 (Base)

    out_uops[1].iq_id = IQ_STD;
    out_uops[1].uop = src_uop;
    out_uops[1].uop.op = UOP_STD;
    out_uops[1].uop.src1_en = false; // STD 数据源修正
    out_uops[1].uop.src2_en = true;  // STD 只用 src2 (Data)
    count = 2;
    break;

  case JALR:
    // JALR -> ADD (PC+4) + JUMP
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_ADD;
    out_uops[0].uop.imm = 4;
    out_uops[0].uop.src1_en = false; // PC+4 不需要 src1
    out_uops[0].uop.src2_en = false; // PC+4 不需要 src2

    out_uops[1].iq_id = IQ_BR;
    out_uops[1].uop = src_uop;
    out_uops[1].uop.op = UOP_JUMP;
    out_uops[1].uop.src1_en = true; // JALR 需要 src1 (Base)
    out_uops[1].uop.dest_en = false;
    count = 2;
    break;

  case JAL:
    // JAL -> ADD (PC+4) + JUMP
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    out_uops[0].uop.op = UOP_ADD;
    out_uops[0].uop.imm = 4;
    out_uops[0].uop.src1_en = false; // PC+4 不需要 src1
    out_uops[0].uop.src2_en = false; // PC+4 不需要 src2

    out_uops[1].iq_id = IQ_BR;
    out_uops[1].uop = src_uop;
    out_uops[1].uop.op = UOP_JUMP;
    out_uops[1].uop.dest_en = false; // 跳转不写寄存器
    count = 2;
    break;

  case AMO:
    if ((src_uop.func7 >> 2) == AmoOp::LR) {
      out_uops[0].iq_id = IQ_LD;
      out_uops[0].uop = src_uop;
      out_uops[0].uop.op = UOP_LOAD;
      out_uops[0].uop.src2_en = false;
      count = 1;
    } else if ((src_uop.func7 >> 2) == AmoOp::SC) {
      // SC -> INT(0) + STA + STD
      out_uops[0].iq_id = IQ_INT;
      out_uops[0].uop = src_uop;
      out_uops[0].uop.op = UOP_ADD; // 预设 0 (假定成功，LSU会覆盖? 或者这里仅仅是占位)
                                    // 实际 SC 的返回值由 LSU Writeback 决定，通常是 Store 成功与否
                                    // 如果这里 INT 写了 rd，后面 LSU 可能会再次写 rd
      out_uops[0].uop.src1_preg = 0; // x0
      out_uops[0].uop.src1_busy = false;
      out_uops[0].uop.imm = 0;
      out_uops[0].uop.src1_en = false;
      out_uops[0].uop.src2_en = false;

      out_uops[1].iq_id = IQ_STA;
      out_uops[1].uop = src_uop;
      out_uops[1].uop.op = UOP_STA;
      out_uops[1].uop.src2_en = false;
      out_uops[1].uop.dest_en = false; // Fix: STA 不写回寄存器

      out_uops[2].iq_id = IQ_STD;
      out_uops[2].uop = src_uop;
      out_uops[2].uop.op = UOP_STD;
      out_uops[2].uop.src1_en = false;
      out_uops[2].uop.dest_en = false; // Fix: STD 不写回寄存器
      count = 3;
    } else {
      // AMO RMW -> LOAD + STA + STD
      out_uops[0].iq_id = IQ_LD;
      out_uops[0].uop = src_uop;
      out_uops[0].uop.op = UOP_LOAD;
      out_uops[0].uop.src2_en = false;

      out_uops[1].iq_id = IQ_STA;
      out_uops[1].uop = src_uop;
      out_uops[1].uop.op = UOP_STA;
      out_uops[1].uop.src2_en = false;
      out_uops[1].uop.dest_en = false; // Fix: STA 不写回寄存器

      out_uops[2].iq_id = IQ_STD;
      out_uops[2].uop = src_uop;
      out_uops[2].uop.op = UOP_STD;
      // 假设 SDU 负责计算，需要原 dest_preg 作为操作数 (数据源)
      // 注意: 这里 src1_preg 被设为 dest_preg，用于读取内存旧值进行原子运算? 
      // 不，Load 结果写到了 dest_preg。STD 需要用到这个 dest_preg (Load Result) 吗?
      // 通常 AMO: Load -> (ALU in LSU or STD?) -> Store
      // 如果计算在 LSU 内部完成 (Atomic)，则 STD 可能只需要传 src2 (rs2)?
      // 代码原意: out_uops[2].uop.src1_preg = src_uop.dest_preg;
      // 这意味着 STD 依赖于 Load 的结果 (dest_preg)。
      // 如果 Load 正确写回了 dest_preg，那么 STD 读取它是对的。
      out_uops[2].uop.src1_preg = src_uop.dest_preg;
      if ((src_uop.func7 >> 2) == AmoOp::SWAP) {
          out_uops[2].uop.src1_busy = false; // Swap doesn't need Load result (Old Val) for Store
          // DEBUG
          if (src_uop.pc == 0xc03870f4) {
             printf("[Dispatch Fix] Triggered for Target PC %x. Clearing src1_busy.\n", src_uop.pc);
          }
      } else {
          out_uops[2].uop.src1_busy = true;
      }
      out_uops[2].uop.dest_en = false; // Fix: STD 不写回寄存器
      count = 3;
    }
    break;

  // 改编自：NOP, CSR, 等
  default: // NOP, CSR, 等
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = src_uop;
    // 特殊指令走整数队列 (IQ_INT)
    switch (src_uop.type) {
    case NOP:
      out_uops[0].uop.op = UOP_ADD;
      break;
    case CSR:
      out_uops[0].uop.op = UOP_CSR;
      break;
    case ECALL:
      out_uops[0].uop.op = UOP_ECALL;
      break;
    case MRET:
      out_uops[0].uop.op = UOP_MRET;
      break;
    case SRET:
      out_uops[0].uop.op = UOP_SRET;
      break;
    case SFENCE_VMA:
      out_uops[0].uop.op = UOP_SFENCE_VMA;
      break;
    case FENCE_I:
      out_uops[0].uop.op = UOP_FENCE_I;
      break;
    case EBREAK:
      out_uops[0].uop.op = UOP_EBREAK;
      break;
    default:
      exit(1);
    }
    count = 1;
    break;
  }
  return count;
}

DispatchIO Dispatch::get_hardware_io() {
  DispatchIO hardware;

  // --- Inputs ---
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.from_ren.valid[i] = in.ren2dis->valid[i];
    hardware.from_ren.uop[i]   = RenDisUop::filter(in.ren2dis->uop[i]);
  }
  hardware.from_rob.ready = in.rob2dis->ready;
  hardware.from_rob.full  = in.rob2dis->stall;
  for (int j = 0; j < IQ_NUM; j++) {
    hardware.from_iss.ready_num[j] = in.iss2dis->ready_num[j];
  }
  hardware.from_back.flush = in.rob_bcast->flush;

  // --- Outputs ---
  hardware.to_ren.ready = out.dis2ren->ready;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.to_rob.valid[i] = out.dis2rob->valid[i];
    hardware.to_rob.uop[i]   = RobUop::filter(out.dis2rob->uop[i]);
  }
  for (int j = 0; j < IQ_NUM; j++) {
    for (int k = 0; k < MAX_IQ_DISPATCH_WIDTH; k++) {
      hardware.to_iss.valid[j][k] = out.dis2iss->req[j][k].valid;
      hardware.to_iss.uop[j][k]   = DisIssUop::filter(out.dis2iss->req[j][k].uop);
    }
  }

  return hardware;
}
