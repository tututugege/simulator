#include "Dispatch.h"
#include "config.h"
#include "util.h"

bool Dispatch::is_preg_woken(wire<PRF_IDX_WIDTH> preg) const {
  for (int w = 0; w < LSU_LOAD_WB_WIDTH; w++) {
    if (in.prf_awake->wake[w].valid && in.prf_awake->wake[w].preg == preg) {
      return true;
    }
  }
  for (int w = 0; w < MAX_WAKEUP_PORTS; w++) {
    if (in.iss_awake->wake[w].valid && in.iss_awake->wake[w].preg == preg) {
      return true;
    }
  }
  return false;
}

void Dispatch::apply_wakeup_to_uop(RenDisIO::RenDisInst &uop) const {
  if (uop.src1_en && is_preg_woken(uop.src1_preg)) {
    uop.src1_busy = false;
  }
  if (uop.src2_en && is_preg_woken(uop.src2_preg)) {
    uop.src2_busy = false;
  }
}

void Dispatch::init() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r[i] = {};
    inst_r_1[i] = {};
    inst_valid[i] = false;
    inst_valid_1[i] = false;
    inst_alloc[i] = {};
    dispatch_success_flags[i] = false;
    dispatch_cache[i].count = 0;
    for (int k = 0; k < MAX_UOPS_PER_INST; k++) {
      dispatch_cache[i].iq_ids[k] = 0;
    }
  }
  for (int k = 0; k < MAX_STQ_DISPATCH_WIDTH; k++) {
    stq_port_owner[k] = -1;
  }
  for (int k = 0; k < MAX_LDQ_DISPATCH_WIDTH; k++) {
    ldq_port_owner[k] = -1;
  }
  std::memset(busy_table, 0, sizeof(busy_table));
  std::memset(busy_table_1, 0, sizeof(busy_table_1));
}

/*
 * comb_begin
 * 功能: 组合阶段开始时复制流水状态和 busy_table 到 *_1 工作副本。
 * 输入依赖: inst_r/inst_valid, busy_table。
 * 输出更新: inst_r_1/inst_valid_1, busy_table_1。
 * 约束: 仅做镜像，不做资源分配/发射决策。
 */
void Dispatch::comb_begin() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r_1[i] = inst_r[i];
    inst_valid_1[i] = inst_valid[i];
  }
  std::memcpy(busy_table_1, busy_table, sizeof(busy_table_1));
}

/*
 * comb_alloc
 * 功能: 预分配 ROB/LDQ/STQ 相关元数据，并生成 dis2rob 初始 valid/uop。
 * 输入依赖: inst_r/inst_valid, in.rob2dis->enq_idx/rob_flag, in.lsu2dis->{stq_tail,stq_tail_flag,stq_free,ldq_free,ldq_alloc_*}, in.dec_bcast->clear_mask。
 * 输出更新: inst_alloc[], out.dis2rob->valid/uop, stq_port_owner[], ldq_port_owner[], out.dis2lsu 的 alloc 请求初值。
 * 约束: 资源不足时该槽位 dis2rob->valid 置 0；br_mask 入队前先清除 clear_mask。
 */
void Dispatch::comb_alloc() {
  int store_alloc_count = 0; // 当前周期已分配的 store 数量
  int load_alloc_count = 0;  // 当前周期已分配的 load 数量
  wire<BR_MASK_WIDTH> clear_mask = in.dec_bcast->clear_mask;

  // 初始化输出
  for (int k = 0; k < MAX_STQ_DISPATCH_WIDTH; k++) {
    out.dis2lsu->alloc_req[k] = false;
    out.dis2lsu->br_mask[k] = 0;
    stq_port_owner[k] = -1;
  }
  for (int k = 0; k < MAX_LDQ_DISPATCH_WIDTH; k++) {
    out.dis2lsu->ldq_alloc_req[k] = false;
    out.dis2lsu->ldq_br_mask[k] = 0;
    ldq_port_owner[k] = -1;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_alloc[i] = DispatchInst::from_ren_dis_inst(inst_r[i]);
    // 入队口强制清理：本拍已解析分支对应 bit 不应继续进入后端缓冲结构。
    inst_alloc[i].br_mask &= ~clear_mask;
    out.dis2rob->valid[i] = inst_valid[i];

    // 分配 ROB ID (重排序缓存索引)
    inst_alloc[i].rob_idx = make_rob_idx(in.rob2dis->enq_idx, i);
    inst_alloc[i].rob_flag = in.rob2dis->rob_flag;
    inst_alloc[i].expect_mask = 0;
    inst_alloc[i].cplt_mask = 0;

    // Load 需要知道之前的 Store
    if (inst_valid[i] && is_load(inst_r[i])) {
      int stop_pos = in.lsu2dis->stq_tail + store_alloc_count;
      inst_alloc[i].stq_idx = stop_pos % STQ_SIZE;
      inst_alloc[i].stq_flag =
          in.lsu2dis->stq_tail_flag ^ ((stop_pos / STQ_SIZE) & 0x1);

      // 检查 Load 队列限制和端口限制
      if (load_alloc_count < in.lsu2dis->ldq_free &&
          load_alloc_count < GLOBAL_IQ_CONFIG[IQ_LD].dispatch_width &&
          load_alloc_count < MAX_LDQ_DISPATCH_WIDTH &&
          in.lsu2dis->ldq_alloc_valid[load_alloc_count]) {
        inst_alloc[i].ldq_idx = in.lsu2dis->ldq_alloc_idx[load_alloc_count];
        ldq_port_owner[load_alloc_count] = i;
        load_alloc_count++;
      } else {
        out.dis2rob->valid[i] = false;
      }
    }

    // 处理 Store 分配
    if (inst_valid[i] && is_store(inst_r[i])) {
      // 检查是否有足够的 STQ 端口和队列空间
      if (store_alloc_count < in.lsu2dis->stq_free &&
          store_alloc_count < MAX_STQ_DISPATCH_WIDTH) {
        // 计算 STQ Index
        int allocated_idx =
            (in.lsu2dis->stq_tail + store_alloc_count) % STQ_SIZE;
        int allocated_pos = in.lsu2dis->stq_tail + store_alloc_count;
        inst_alloc[i].stq_idx = allocated_idx;
        inst_alloc[i].stq_flag =
            in.lsu2dis->stq_tail_flag ^ ((allocated_pos / STQ_SIZE) & 0x1);

        stq_port_owner[store_alloc_count] = i;

        store_alloc_count++;
      } else {
        out.dis2rob->valid[i] = false;
      }
    }

    out.dis2rob->uop[i] = inst_alloc[i].to_dis_rob_inst();
  }
}

// BusyTable owner: Dispatch
/*
 * comb_wake
 * 功能: 根据 busy_table 与唤醒总线更新 inst_alloc 源操作数 busy 状态。
 * 输入依赖: busy_table, in.prf_awake, in.iss_awake, inst_alloc, out.dis2rob->valid。
 * 输出更新: inst_alloc[].src1_busy/src2_busy。
 * 约束: 同拍更早槽位若写同一 preg，则后续槽位对应源 busy 必须保持 true（防止过早就绪）。
 */
void Dispatch::comb_wake() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (!out.dis2rob->valid[i]) {
      continue;
    }

    auto &uop = inst_alloc[i];
    uop.src1_busy = uop.src1_en ? busy_table[uop.src1_preg] : false;
    uop.src2_busy = uop.src2_en ? busy_table[uop.src2_preg] : false;
    apply_wakeup_to_uop(uop);

    for (int j = 0; j < i; j++) {
      if (!out.dis2rob->valid[j] || !inst_alloc[j].dest_en) {
        continue;
      }
      if (inst_alloc[j].dest_areg == 0) {
        continue;
      }

      if (uop.src1_en && uop.src1_preg == inst_alloc[j].dest_preg) {
        uop.src1_busy = true;
      }
      if (uop.src2_en && uop.src2_preg == inst_alloc[j].dest_preg) {
        uop.src2_busy = true;
      }
    }
  }
}

/*
 * comb_dispatch
 * 功能: 拆分宏指令为 uop 并尝试写入各 IQ 请求端口，同时缓存拆分元数据供 comb_fire 回滚。
 * 输入依赖: inst_valid, inst_alloc, in.iss2dis->ready_num, GLOBAL_IQ_CONFIG（dispatch_width）, decompose_inst()。
 * 输出更新: out.dis2iss->req[][], dispatch_success_flags[], dispatch_cache[], out.dis2rob->uop[i].expect_mask/cplt_mask。
 * 约束: 按槽位顺序分配 IQ 端口；任一槽位不满足容量后停止后续槽位分配。
 */
void Dispatch::comb_dispatch() {
  // 1. 清空输出 req
  for (int i = 0; i < IQ_NUM; i++) {
    for (int w = 0; w < MAX_IQ_DISPATCH_WIDTH; w++) {
      out.dis2iss->req[i][w].valid = false;
    }
  }

  int iq_usage[IQ_NUM] = {0};

  for (int i = 0; i < DECODE_WIDTH; i++) {
    dispatch_success_flags[i] = false;
    dispatch_cache[i].count = 0; // 重置计数

    if (!inst_valid[i]) {
      dispatch_success_flags[i] = true;
      continue;
    }

    // === 1. 临时拆分 (Full Data) ===
    // 在栈上分配，用完即弃，不占用类成员空间
    UopPacket temp_uops[MAX_UOPS_PER_INST];
    int cnt = decompose_inst(inst_alloc[i], temp_uops);

    // === 2. 相当于中间变量 存入缓存 ===
    dispatch_cache[i].count = cnt;
    wire<ROB_CPLT_MASK_WIDTH> expect_mask = 0;
    for (int k = 0; k < cnt; k++) {
      expect_mask |= rob_cplt_mask_from_iq(temp_uops[k].iq_id);
    }
    if (decode_inst_type(inst_alloc[i].type) == AMO &&
        ((inst_alloc[i].func7 >> 2) == AmoOp::SC)) {
      // SC returns architectural 0/1 through load-like wb path and still has
      // STD completion.
      expect_mask = ROB_CPLT_G0 | ROB_CPLT_G2;
    }
    inst_alloc[i].expect_mask = expect_mask;
    inst_alloc[i].cplt_mask = 0;
    for (int k = 0; k < cnt; k++) {
      dispatch_cache[i].iq_ids[k] = temp_uops[k].iq_id;
    }

    // === 3. 检查容量 ===
    bool fit = true;
    for (int k = 0; k < cnt; k++) {
      int target = temp_uops[k].iq_id;
      // 直接查表！不需要 Isu 告诉它，它自己就能看 config.h
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
      out.dis2rob->uop[i].expect_mask = expect_mask;
      out.dis2rob->uop[i].cplt_mask = 0;
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

/*
 * comb_fire
 * 功能: 计算最终 dis_fire 与 dis2ren.ready，提交 busy_table 更新，并根据 fire 成败确认/回滚 IQ 与 LSU 请求。
 * 输入依赖: out.dis2rob->valid, dispatch_success_flags, dispatch_cache, inst_valid/inst_r/inst_alloc, in.rob2dis, in.rob_bcast, in.dec_bcast, in.prf_awake, in.iss_awake。
 * 输出更新: out.dis2rob->dis_fire[], out.dis2ren->ready, out.dis2iss->req[][]（回滚后结果）, out.dis2lsu->{alloc_req/ldq_alloc_req/...}, busy_table_1。
 * 约束: flush/mispred/stall 阻断发射；CSR/AMO 需满足串行化条件；older 指令阻塞后续槽位发射。
 */
void Dispatch::comb_fire() {
  enum Dis2RenBlockReason {
    DIS2REN_BLOCK_NONE = 0,
    DIS2REN_BLOCK_FLUSH_STALL,
    DIS2REN_BLOCK_ROB,
    DIS2REN_BLOCK_SERIALIZE,
    DIS2REN_BLOCK_DISPATCH,
    DIS2REN_BLOCK_OLDER,
  };
  enum Dis2RenDispatchDetail {
    DIS2REN_DISPATCH_DETAIL_NONE = 0,
    DIS2REN_DISPATCH_DETAIL_LDQ,
    DIS2REN_DISPATCH_DETAIL_STQ,
    DIS2REN_DISPATCH_DETAIL_IQ_INT,
    DIS2REN_DISPATCH_DETAIL_IQ_LD,
    DIS2REN_DISPATCH_DETAIL_IQ_STA,
    DIS2REN_DISPATCH_DETAIL_IQ_STD,
    DIS2REN_DISPATCH_DETAIL_IQ_BR,
    DIS2REN_DISPATCH_DETAIL_IQ_UNKNOWN,
    DIS2REN_DISPATCH_DETAIL_OTHER,
  };

  bool pre_stall = false;
  bool pre_fire = false;
  bool serializing_barrier = false;
  int dis2ren_block_reason = DIS2REN_BLOCK_NONE;
#ifdef CONFIG_PERF_COUNTER
  int dis2ren_dispatch_detail = DIS2REN_DISPATCH_DETAIL_NONE;
  auto classify_dispatch_block = [&](int slot_idx) -> int {
    if (slot_idx < 0 || slot_idx >= DECODE_WIDTH) {
      return DIS2REN_DISPATCH_DETAIL_OTHER;
    }
    if (!inst_valid[slot_idx]) {
      return DIS2REN_DISPATCH_DETAIL_OTHER;
    }

    // LSU allocation reject in comb_alloc.
    if (!out.dis2rob->valid[slot_idx]) {
      if (is_load(inst_r[slot_idx])) {
        return DIS2REN_DISPATCH_DETAIL_LDQ;
      }
      if (is_store(inst_r[slot_idx])) {
        return DIS2REN_DISPATCH_DETAIL_STQ;
      }
    }

    // IQ capacity/port reject in comb_dispatch.
    if (!dispatch_success_flags[slot_idx]) {
      int iq_used[IQ_NUM] = {0};
      for (int j = 0; j < slot_idx; j++) {
        if (!inst_valid[j] || !dispatch_success_flags[j]) {
          continue;
        }
        int cnt_prev = dispatch_cache[j].count;
        for (int k = 0; k < cnt_prev; k++) {
          int iq = dispatch_cache[j].iq_ids[k];
          if (iq >= 0 && iq < IQ_NUM) {
            iq_used[iq]++;
          }
        }
      }

      int cnt = dispatch_cache[slot_idx].count;
      for (int k = 0; k < cnt; k++) {
        int iq = dispatch_cache[slot_idx].iq_ids[k];
        if (iq < 0 || iq >= IQ_NUM) {
          continue;
        }
        int port_limit = GLOBAL_IQ_CONFIG[iq].dispatch_width;
        if (iq_used[iq] >= port_limit ||
            iq_used[iq] >= in.iss2dis->ready_num[iq]) {
          switch (iq) {
          case IQ_INT:
            return DIS2REN_DISPATCH_DETAIL_IQ_INT;
          case IQ_LD:
            return DIS2REN_DISPATCH_DETAIL_IQ_LD;
          case IQ_STA:
            return DIS2REN_DISPATCH_DETAIL_IQ_STA;
          case IQ_STD:
            return DIS2REN_DISPATCH_DETAIL_IQ_STD;
          case IQ_BR:
            return DIS2REN_DISPATCH_DETAIL_IQ_BR;
          default:
            return DIS2REN_DISPATCH_DETAIL_IQ_UNKNOWN;
          }
        }
      }
      return DIS2REN_DISPATCH_DETAIL_IQ_UNKNOWN;
    }

    return DIS2REN_DISPATCH_DETAIL_OTHER;
  };
#endif
  wire<BR_MASK_WIDTH> clear_mask = in.dec_bcast->clear_mask;
  bool global_flush =
      in.rob_bcast->flush || in.dec_bcast->mispred || in.rob2dis->stall;

  std::memcpy(busy_table_1, busy_table, sizeof(busy_table_1));
  for (int w = 0; w < LSU_LOAD_WB_WIDTH; w++) {
    if (in.prf_awake->wake[w].valid) {
      busy_table_1[in.prf_awake->wake[w].preg] = false;
    }
  }
  for (int w = 0; w < MAX_WAKEUP_PORTS; w++) {
    if (in.iss_awake->wake[w].valid) {
      busy_table_1[in.iss_awake->wake[w].preg] = false;
    }
  }

  // === 步骤 1: 计算 Fire 信号 (确认分派) ===
  for (int i = 0; i < DECODE_WIDTH; i++) {
    bool older_block = pre_stall;
    bool is_atomic_inst =
        inst_valid[i] && decode_inst_type(inst_r[i].type) == AMO;
    bool csr_blocked = false;
    bool atomic_blocked = false;
    bool basic_fire = out.dis2rob->valid[i] &&
                      dispatch_success_flags[i] && // IQ 检查通过
                      in.rob2dis->ready &&         // ROB 有空间
                      !pre_stall && !global_flush;

    // 特殊检查：CSR
    if (is_CSR(inst_r[i].type)) {
      if (!in.rob2dis->empty || pre_fire) {
        basic_fire = false;
        csr_blocked = true;
      }
    }
    // 原子指令串行化：仅允许在 ROB 空且本拍无更早指令发射时进入后端。
    if (is_atomic_inst) {
      if (!in.rob2dis->empty || pre_fire) {
        basic_fire = false;
        atomic_blocked = true;
      }
    }

    out.dis2rob->dis_fire[i] = basic_fire;

    if (inst_valid[i] && !basic_fire) {
      if (dis2ren_block_reason == DIS2REN_BLOCK_NONE) {
        if (global_flush) {
          dis2ren_block_reason = DIS2REN_BLOCK_FLUSH_STALL;
        } else if (!in.rob2dis->ready) {
          dis2ren_block_reason = DIS2REN_BLOCK_ROB;
        } else if (csr_blocked || atomic_blocked || serializing_barrier) {
          dis2ren_block_reason = DIS2REN_BLOCK_SERIALIZE;
        } else if (older_block) {
          dis2ren_block_reason = DIS2REN_BLOCK_OLDER;
        } else {
          dis2ren_block_reason = DIS2REN_BLOCK_DISPATCH;
#ifdef CONFIG_PERF_COUNTER
          dis2ren_dispatch_detail = classify_dispatch_block(i);
#endif
        }
      }
      pre_stall = true;
    }

    // 原子指令本拍独占发射，后续槽位全部阻塞。
    if (basic_fire && is_atomic_inst) {
      pre_stall = true;
      serializing_barrier = true;
    }

    if (basic_fire)
      pre_fire = true;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (out.dis2rob->dis_fire[i] && inst_r[i].dest_en) {
      busy_table_1[inst_r[i].dest_preg] = true;
    }
  }

  // 更新 Rename 单元的 Ready 信号
  out.dis2ren->ready = !pre_stall;

  // === 步骤 2: 撤销无效的 IQ 请求 (回滚) ===
  int iq_slot_idx[IQ_NUM] = {0};

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (!inst_valid[i])
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
    int inst_idx = stq_port_owner[k];
    if (inst_idx >= 0 && out.dis2rob->dis_fire[inst_idx]) {
      out.dis2lsu->alloc_req[k] = true;
      out.dis2lsu->br_mask[k] =
          out.dis2rob->uop[inst_idx].br_mask & ~clear_mask;
      out.dis2lsu->rob_idx[k] = out.dis2rob->uop[inst_idx].rob_idx;
      out.dis2lsu->rob_flag[k] = out.dis2rob->uop[inst_idx].rob_flag;
      out.dis2lsu->stq_flag[k] = out.dis2rob->uop[inst_idx].stq_flag;
      out.dis2lsu->func3[k] = out.dis2rob->uop[inst_idx].func3;
    }
  }

  // === 步骤 4: 更新 LDQ 的 Fire 信号 ===
  for (int k = 0; k < MAX_LDQ_DISPATCH_WIDTH; k++) {
    int inst_idx = ldq_port_owner[k];
    if (inst_idx >= 0 && out.dis2rob->dis_fire[inst_idx]) {
      out.dis2lsu->ldq_alloc_req[k] = true;
      out.dis2lsu->ldq_idx[k] = out.dis2rob->uop[inst_idx].ldq_idx;
      out.dis2lsu->ldq_br_mask[k] =
          out.dis2rob->uop[inst_idx].br_mask & ~clear_mask;
      out.dis2lsu->ldq_rob_idx[k] = out.dis2rob->uop[inst_idx].rob_idx;
      out.dis2lsu->ldq_rob_flag[k] = out.dis2rob->uop[inst_idx].rob_flag;
    }
  }

#ifdef CONFIG_PERF_COUNTER
  if (!out.dis2ren->ready) {
    ctx->perf.dis2ren_not_ready_cycles++;
    switch (dis2ren_block_reason) {
    case DIS2REN_BLOCK_FLUSH_STALL:
      ctx->perf.dis2ren_not_ready_flush_cycles++;
      break;
    case DIS2REN_BLOCK_ROB:
      ctx->perf.dis2ren_not_ready_rob_cycles++;
      break;
    case DIS2REN_BLOCK_SERIALIZE:
      ctx->perf.dis2ren_not_ready_serialize_cycles++;
      break;
    case DIS2REN_BLOCK_DISPATCH:
      ctx->perf.dis2ren_not_ready_dispatch_cycles++;
      switch (dis2ren_dispatch_detail) {
      case DIS2REN_DISPATCH_DETAIL_LDQ:
        ctx->perf.dis2ren_not_ready_dispatch_ldq_cycles++;
        break;
      case DIS2REN_DISPATCH_DETAIL_STQ:
        ctx->perf.dis2ren_not_ready_dispatch_stq_cycles++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_INT:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_iq_detail[IQ_INT]++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_LD:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_iq_detail[IQ_LD]++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_STA:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_iq_detail[IQ_STA]++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_STD:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_iq_detail[IQ_STD]++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_BR:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_iq_detail[IQ_BR]++;
        break;
      case DIS2REN_DISPATCH_DETAIL_IQ_UNKNOWN:
        ctx->perf.dis2ren_not_ready_dispatch_iq_cycles++;
        ctx->perf.dis2ren_not_ready_dispatch_other_cycles++;
        break;
      case DIS2REN_DISPATCH_DETAIL_OTHER:
      case DIS2REN_DISPATCH_DETAIL_NONE:
      default:
        ctx->perf.dis2ren_not_ready_dispatch_other_cycles++;
        break;
      }
      break;
    case DIS2REN_BLOCK_OLDER:
      ctx->perf.dis2ren_not_ready_older_cycles++;
      break;
    default:
      ctx->perf.dis2ren_not_ready_dispatch_cycles++;
      break;
    }
  }

  bool is_core_bound_rob[DECODE_WIDTH] = {false};
  bool is_core_bound_iq[DECODE_WIDTH] = {false};
  bool is_mem_l1_bound[DECODE_WIDTH] = {false};
  bool is_mem_ext_bound[DECODE_WIDTH] = {false};
  bool is_mem_ldq_full[DECODE_WIDTH] = {false};
  bool is_mem_stq_full[DECODE_WIDTH] = {false};
  bool any_rob_full_stall = false;
  bool any_iq_full_stall = false;
  bool any_ldq_full_stall = false;
  bool any_stq_full_stall = false;

  // Analyze stall reasons for each slot
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (!out.dis2rob->dis_fire[i] && inst_valid[i]) {

      // Priority: ROB > LSU > IQ
      // If ROB is full/stalled
      if (!in.rob2dis->ready) { // ROB Full
        any_rob_full_stall = true;
        if (in.rob2dis->tma.head_is_memory && in.rob2dis->tma.head_not_ready) {
          if (in.rob2dis->tma.head_is_miss) {
            is_mem_ext_bound[i] = true;
          } else {
            is_mem_l1_bound[i] = true;
          }
        } else {
          is_core_bound_rob[i] = true;
        }
      }
      // If LSU allocation failed in comb_alloc (LDQ/STQ headroom or per-cycle
      // dispatch bandwidth), account it as memory pressure first.
      else if (!out.dis2rob->valid[i]) {
        if (is_load(inst_r[i])) {
          is_mem_l1_bound[i] = true;
          is_mem_ldq_full[i] = true;
          any_ldq_full_stall = true;
        } else if (is_store(inst_r[i])) {
          is_mem_l1_bound[i] = true;
          is_mem_stq_full[i] = true;
          any_stq_full_stall = true;
        } else {
          is_core_bound_iq[i] = true;
          any_iq_full_stall = true;
        }
      }
      // If Dispatch Logic failed (IQ check or LSU check)
      else if (!dispatch_success_flags[i]) {
        bool lsu_stall = false;
        if (is_load(inst_r[i])) {
          if (in.lsu2dis->ldq_free == 0) {
            lsu_stall = true;
            is_mem_ldq_full[i] = true;
            any_ldq_full_stall = true;
          }
        } else if (is_store(inst_r[i])) {
          if (in.lsu2dis->stq_free == 0) {
            lsu_stall = true;
            is_mem_stq_full[i] = true;
            any_stq_full_stall = true;
          }
        }

        if (lsu_stall) {
          is_mem_l1_bound[i] = true;
        } else {
          is_core_bound_iq[i] = true;
          any_iq_full_stall = true;
        }
      }
    }
  }

  if (any_rob_full_stall) {
    ctx->perf.stall_rob_full_cycles++;
  }
  if (any_iq_full_stall) {
    ctx->perf.stall_iq_full_cycles++;
  }
  if (any_ldq_full_stall) {
    ctx->perf.stall_ldq_full_cycles++;
  }
  if (any_stq_full_stall) {
    ctx->perf.stall_stq_full_cycles++;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (out.dis2rob->dis_fire[i]) {
      ctx->perf.slots_issued++;
    } else if (inst_valid[i]) {
      ctx->perf.slots_backend_bound++;

      if (is_mem_l1_bound[i]) {
        ctx->perf.slots_mem_bound_lsu++;
        ctx->perf.slots_mem_l1_bound++;
        if (is_mem_ldq_full[i]) {
          ctx->perf.slots_mem_bound_ldq_full++;
        }
        if (is_mem_stq_full[i]) {
          ctx->perf.slots_mem_bound_stq_full++;
        }
      } else if (is_mem_ext_bound[i]) {
        ctx->perf.slots_mem_bound_lsu++;
        ctx->perf.slots_mem_ext_bound++;
      } else if (is_core_bound_rob[i]) {
        ctx->perf.slots_core_bound_rob++;
      } else if (is_core_bound_iq[i]) {
        ctx->perf.slots_core_bound_iq++;
      } else {
        // Default to IQ bound if no other reason identified for Backend Bound
        ctx->perf.slots_core_bound_iq++;
      }
    } else {
      ctx->perf.slots_frontend_bound++;
      if (ctx->perf.pending_squash_mispred_slots > 0) {
        ctx->perf.slots_frontend_recovery_mispred++;
        ctx->perf.slots_squash_waste++;
        ctx->perf.pending_squash_mispred_slots--;
      } else if (ctx->perf.pending_squash_flush_slots > 0) {
        ctx->perf.slots_frontend_recovery_flush++;
        ctx->perf.slots_squash_waste++;
        ctx->perf.pending_squash_flush_slots--;
      } else {
        ctx->perf.slots_frontend_pure++;
        if (ctx->perf.icache_busy)
          ctx->perf.slots_fetch_latency++;
        else
          ctx->perf.slots_fetch_bandwidth++;
      }
    }
  }
#endif
}

/*
 * comb_pipeline
 * 功能: 推进 Dispatch 流水寄存器，处理 flush/mispred 清空与背压保留项更新。
 * 输入依赖: in.ren2dis, out.dis2ren->ready, out.dis2rob->dis_fire, inst_valid/inst_r, inst_alloc, in.rob_bcast->flush, in.dec_bcast->{mispred, clear_mask}。
 * 输出更新: inst_valid_1[], inst_r_1[]（保留项 busy 状态可被同拍唤醒修正）。
 * 约束: flush/mispred 时全部置无效；ready=1 时采样新输入；保留项及新采样项都需清除 clear_mask。
 */
void Dispatch::comb_pipeline() {
  wire<BR_MASK_WIDTH> clear_mask = in.dec_bcast->clear_mask;

  if (in.rob_bcast->flush || in.dec_bcast->mispred) {
#ifdef CONFIG_PERF_COUNTER
    uint64_t killed = 0;
    for (int i = 0; i < DECODE_WIDTH; i++) {
      if (inst_valid[i]) {
        killed++;
      }
    }
    if (killed > 0) {
      if (in.rob_bcast->flush) {
        ctx->perf.squash_flush_dis += killed;
        ctx->perf.squash_flush_total += killed;
        ctx->perf.pending_squash_flush_slots += killed;
      } else {
        ctx->perf.squash_mispred_dis += killed;
        ctx->perf.squash_mispred_total += killed;
        ctx->perf.pending_squash_mispred_slots += killed;
      }
    }
#endif
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (in.rob_bcast->flush || in.dec_bcast->mispred) {
      inst_valid_1[i] = false;
      continue;
    }
    if (out.dis2ren->ready) {
      inst_valid_1[i] = in.ren2dis->valid[i];
      inst_r_1[i] = in.ren2dis->uop[i];
      inst_r_1[i].br_mask &= ~clear_mask;
      continue;
    }

    inst_valid_1[i] = inst_valid[i] && !out.dis2rob->dis_fire[i];
    if (inst_valid_1[i]) {
      inst_r_1[i].src1_busy = inst_alloc[i].src1_busy;
      inst_r_1[i].src2_busy = inst_alloc[i].src2_busy;
      apply_wakeup_to_uop(inst_r_1[i]);
      inst_r_1[i].br_mask &= ~clear_mask;
    }
  }
}

void Dispatch::seq() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
    inst_valid[i] = inst_valid_1[i];
  }
  std::memcpy(busy_table, busy_table_1, sizeof(busy_table));
}

DisIssIO::DisIssUop Dispatch::make_dis_iss_uop(const DispatchInst &inst) const {
  DisIssIO::DisIssUop uop;
  uop.dest_preg = inst.dest_preg;
  uop.src1_preg = inst.src1_preg;
  uop.src2_preg = inst.src2_preg;
  uop.ftq_idx = inst.ftq_idx;
  uop.ftq_offset = inst.ftq_offset;
  uop.is_atomic = inst.is_atomic;
  uop.dest_en = inst.dest_en;
  uop.src1_en = inst.src1_en;
  uop.src2_en = inst.src2_en;
  uop.src1_busy = inst.src1_busy;
  uop.src2_busy = inst.src2_busy;
  uop.src1_is_pc = inst.src1_is_pc;
  uop.src2_is_imm = inst.src2_is_imm;
  uop.func3 = inst.func3;
  uop.func7 = inst.func7;
  uop.imm = inst.imm;
  uop.br_id = inst.br_id;
  uop.br_mask = inst.br_mask;
  uop.csr_idx = inst.csr_idx;
  uop.rob_idx = inst.rob_idx;
  uop.stq_idx = inst.stq_idx;
  uop.stq_flag = inst.stq_flag;
  uop.ldq_idx = inst.ldq_idx;
  uop.rob_flag = inst.rob_flag;
  uop.dbg = inst.dbg;
  return uop;
}

int Dispatch::decompose_inst(const DispatchInst &inst, UopPacket *out_uops) {
  int count = 0;
  InstType type = decode_inst_type(inst.type);

  switch (type) {
  case ADD:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_ADD;
    count = 1;
    break;
  case MUL:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_MUL;
    count = 1;
    break;
  case DIV:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_DIV;
    count = 1;
    break;
  case FP:
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_FP;
    count = 1;
    break;
  case BR:
    out_uops[0].iq_id = IQ_BR;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_BR;
    count = 1;
    break;

  case LOAD:
    out_uops[0].iq_id = IQ_LD;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_LOAD;
    count = 1;
    break;

  case STORE:
    // 拆分为 STA + STD
    out_uops[0].iq_id = IQ_STA;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_STA;
    out_uops[0].uop.src2_en = false; // STA 只用 src1 (Base)

    out_uops[1].iq_id = IQ_STD;
    out_uops[1].uop = make_dis_iss_uop(inst);
    out_uops[1].uop.op = UOP_STD;
    out_uops[1].uop.src1_en = false; // STD 数据源修正
    out_uops[1].uop.src2_en = true;  // STD 只用 src2 (Data)
    count = 2;
    break;

  case JALR:
    // JALR -> ADD (PC+4) + JUMP
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_ADD;
    out_uops[0].uop.imm = 4;
    out_uops[0].uop.src1_en = false; // PC+4 不需要 src1
    out_uops[0].uop.src2_en = false; // PC+4 不需要 src2

    out_uops[1].iq_id = IQ_BR;
    out_uops[1].uop = make_dis_iss_uop(inst);
    out_uops[1].uop.op = UOP_JUMP;
    out_uops[1].uop.src1_en = true; // JALR 需要 src1 (Base)
    out_uops[1].uop.dest_en = false;
    count = 2;
    break;

  case JAL:
    // JAL -> ADD (PC+4) + JUMP
    out_uops[0].iq_id = IQ_INT;
    out_uops[0].uop = make_dis_iss_uop(inst);
    out_uops[0].uop.op = UOP_ADD;
    out_uops[0].uop.imm = 4;
    out_uops[0].uop.src1_en = false; // PC+4 不需要 src1
    out_uops[0].uop.src2_en = false; // PC+4 不需要 src2

    out_uops[1].iq_id = IQ_BR;
    out_uops[1].uop = make_dis_iss_uop(inst);
    out_uops[1].uop.op = UOP_JUMP;
    out_uops[1].uop.dest_en = false; // 跳转不写寄存器
    count = 2;
    break;

  case AMO:
    if ((inst.func7 >> 2) == AmoOp::LR) {
      out_uops[0].iq_id = IQ_LD;
      out_uops[0].uop = make_dis_iss_uop(inst);
      out_uops[0].uop.op = UOP_LOAD;
      out_uops[0].uop.src2_en = false;
      count = 1;
    } else if ((inst.func7 >> 2) == AmoOp::SC) {
      // SC -> STA + STD
      // SC 的 0/1 返回值由 LSU 在地址阶段决定后，经 LSU load-like wb
      // 路径返回；这里不再保留 INT 占位 uop。
      out_uops[0].iq_id = IQ_STA;
      out_uops[0].uop = make_dis_iss_uop(inst);
      out_uops[0].uop.op = UOP_STA;
      out_uops[0].uop.src2_en = false;
      out_uops[0].uop.dest_en =
          true; // Reuse LSU STA wb port to write SC result (0/1)

      out_uops[1].iq_id = IQ_STD;
      out_uops[1].uop = make_dis_iss_uop(inst);
      out_uops[1].uop.op = UOP_STD;
      out_uops[1].uop.is_atomic = true;
      out_uops[1].uop.src1_en = false;
      out_uops[1].uop.dest_en = false;
      count = 2;
    } else {
      // AMO RMW -> LOAD + STA + STD
      out_uops[0].iq_id = IQ_LD;
      out_uops[0].uop = make_dis_iss_uop(inst);
      out_uops[0].uop.op = UOP_LOAD;
      out_uops[0].uop.src2_en = false;

      out_uops[1].iq_id = IQ_STA;
      out_uops[1].uop = make_dis_iss_uop(inst);
      out_uops[1].uop.op = UOP_STA;
      out_uops[1].uop.src2_en = false;
      out_uops[1].uop.dest_en = false; // Fix: STA 不写回寄存器

      out_uops[2].iq_id = IQ_STD;
      out_uops[2].uop = make_dis_iss_uop(inst);
      out_uops[2].uop.op = UOP_STD;
      out_uops[2].uop.is_atomic = true;
      // 假设 SDU 负责计算，需要原 dest_preg 作为操作数 (数据源)
      // 注意: 这里 src1_preg 被设为 dest_preg，用于读取内存旧值进行原子运算?
      // 不，Load 结果写到了 dest_preg。STD 需要用到这个 dest_preg (Load Result)
      // 吗? 通常 AMO: Load -> (ALU in LSU or STD?) -> Store 如果计算在 LSU
      // 内部完成 (Atomic)，则 STD 可能只需要传 src2 (rs2)? 代码原意:
      // out_uops[2].uop.src1_preg = src_uop.dest_preg; 这意味着 STD 依赖于 Load
      // 的结果 (dest_preg)。 如果 Load 正确写回了 dest_preg，那么 STD
      // 读取它是对的。
      out_uops[2].uop.src1_preg = inst.dest_preg;
      if ((inst.func7 >> 2) == AmoOp::SWAP) {
        out_uops[2].uop.src1_busy =
            false; // Swap doesn't need Load result (Old Val) for Store
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
    out_uops[0].uop = make_dis_iss_uop(inst);
    // 特殊指令走整数队列 (IQ_INT)
    switch (type) {
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
    case WFI:
      out_uops[0].uop.op = UOP_WFI;
      break;
    default:
      Assert(0 && "unknown instruction");
    }
    count = 1;
    break;
  }
  return count;
}
