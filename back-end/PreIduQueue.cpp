#include "PreIduQueue.h"
#include "types.h"

#include <cstring>

static InstructionBufferEntry push_entries[FETCH_WIDTH];
static int push_count = 0;

static void fill_ftq_pc_resp(FtqPcReadResp &resp, const FTQEntry &entry,
                             const FtqPcReadReq &req) {
  resp = {};
  if (!req.valid) {
    return;
  }

  resp.valid = true;
  resp.entry_valid = entry.valid;
  resp.pc = entry.start_pc + (req.ftq_offset << 2);
  resp.pred_taken = entry.pred_taken_mask[req.ftq_offset];
  resp.next_pc = entry.next_pc;
}

int PreIduQueue::ftq_alloc() {
  if (ftq_count_1 >= FTQ_SIZE) {
    return -1;
  }
  int idx = ftq_tail_1;
  ftq_tail_1 = (ftq_tail_1 + 1) % FTQ_SIZE;
  ftq_count_1++;
  return idx;
}

void PreIduQueue::ftq_pop(int pop_cnt) {
  if (pop_cnt <= 0) {
    return;
  }
  for (int i = 0; i < pop_cnt; i++) {
    if (ftq_count_1 <= 0) {
      break;
    }
    ftq_head_1 = (ftq_head_1 + 1) % FTQ_SIZE;
    ftq_count_1--;
  }
}

void PreIduQueue::ftq_recover(int new_tail) {
  int normalized_tail = ((new_tail % FTQ_SIZE) + FTQ_SIZE) % FTQ_SIZE;
  int discarded = (ftq_tail_1 - normalized_tail + FTQ_SIZE) % FTQ_SIZE;
  for (int i = 0; i < discarded; i++) {
    int idx = (normalized_tail + i) % FTQ_SIZE;
    ftq_entries_1[idx] = FTQEntry();
  }
  ftq_tail_1 = normalized_tail;
  ftq_count_1 -= discarded;
}

void PreIduQueue::ftq_flush() {
  ftq_head_1 = 0;
  ftq_tail_1 = 0;
  ftq_count_1 = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_entries_1[i] = FTQEntry();
  }
}

void PreIduQueue::init() {
  ibuf.init();
  ibuf_1.init();
  ftq_head = ftq_tail = ftq_count = 0;
  ftq_head_1 = ftq_tail_1 = ftq_count_1 = 0;
  push_count = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_entries[i] = FTQEntry();
    ftq_entries_1[i] = FTQEntry();
  }
}

/*
 * comb_begin
 * 功能: 组合阶段起始镜像，将当前拍状态复制到 *_1 工作副本，并准备
 * issue/pre2front 默认输出。 输入依赖: ibuf, ftq_head/tail/count, ftq_entries。
 * 输出更新: ibuf_1, ftq_head_1/ftq_tail_1/ftq_count_1, ftq_entries_1,
 *          out.issue->entries, out.pre2front->ready/fire, push_count。
 * 约束: 仅做状态镜像与默认驱动，不进行 front 接收、recover/pop/push 决策。
 */
void PreIduQueue::comb_begin() {
  ibuf_1 = ibuf;
  ftq_head_1 = ftq_head;
  ftq_tail_1 = ftq_tail;
  ftq_count_1 = ftq_count;
  std::memcpy(ftq_entries_1, ftq_entries, sizeof(ftq_entries));

  for (auto &e : out.issue->entries) {
    e = {};
  }
  int n = ibuf.count() < DECODE_WIDTH ? ibuf.count() : DECODE_WIDTH;
  for (int i = 0; i < n; i++) {
    out.issue->entries[i] = ibuf.peek(i);
  }
#ifdef CONFIG_PERF_COUNTER
  if (ctx != nullptr) {
    ctx->perf.ib_consume_available_slots += static_cast<uint64_t>(n);
  }
#endif

  out.pre2front->ready = false;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.pre2front->fire[i] = false;
  }

  push_count = 0;
}

/*
 * comb_accept_front
 * 功能: 根据 IBUF/FTQ 可用性决定是否接收 front2pre，并在接收时生成 FTQ 新项与
 * IBUF 推入条目缓存。 输入依赖: in.front2pre, in.rob_bcast->flush,
 * in.idu_br_latch->mispred, ibuf, ftq_count。 输出更新: out.pre2front->ready/fire,
 * ftq_entries_1[alloc_idx], ftq_tail_1/ftq_count_1, push_entries[],
 * push_count。 约束: flush/mispred 优先禁止接收；仅在 ready
 * 且有有效输入时接收；同拍最多分配 1 个 FTQ entry。
 */
void PreIduQueue::comb_accept_front() {
  if (in.idu_br_latch->mispred || in.rob_bcast->flush) {
    out.pre2front->ready = false;
    return;
  }

  int incoming_valid_num = 0;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    incoming_valid_num += in.front2pre->valid[i] ? 1 : 0;
  }

  bool ftq_ok = (ftq_count < FTQ_SIZE);
  bool ib_ok = ibuf.can_accept(incoming_valid_num);
  out.pre2front->ready = ftq_ok && ib_ok;
  if (ctx != nullptr && incoming_valid_num > 0 && !out.pre2front->ready) {
    if (!ib_ok) {
      ctx->perf.ib_blocked_cycles++;
    }
    if (!ftq_ok) {
      ctx->perf.ftq_blocked_cycles++;
    }
  }
  if (!out.pre2front->ready || incoming_valid_num == 0) {
    return;
  }

  FTQEntry ftq_entry;
  ftq_entry.start_pc = in.front2pre->pc[0];
  ftq_entry.next_pc = in.front2pre->predict_next_fetch_address[0];
  for (int i = 0; i < FETCH_WIDTH; i++) {
    ftq_entry.pred_taken_mask[i] = in.front2pre->predict_dir[i];
    ftq_entry.alt_pred[i] = in.front2pre->alt_pred[i];
    ftq_entry.altpcpn[i] = in.front2pre->altpcpn[i];
    ftq_entry.pcpn[i] = in.front2pre->pcpn[i];
    ftq_entry.sc_used[i] = in.front2pre->sc_used[i];
    ftq_entry.sc_pred[i] = in.front2pre->sc_pred[i];
    ftq_entry.sc_sum[i] = in.front2pre->sc_sum[i];
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      ftq_entry.sc_idx[i][t] = in.front2pre->sc_idx[i][t];
    }
    ftq_entry.loop_used[i] = in.front2pre->loop_used[i];
    ftq_entry.loop_hit[i] = in.front2pre->loop_hit[i];
    ftq_entry.loop_pred[i] = in.front2pre->loop_pred[i];
    ftq_entry.loop_idx[i] = in.front2pre->loop_idx[i];
    ftq_entry.loop_tag[i] = in.front2pre->loop_tag[i];
    for (int j = 0; j < 4; j++) {
      ftq_entry.tage_idx[i][j] = in.front2pre->tage_idx[i][j];
      ftq_entry.tage_tag[i][j] = in.front2pre->tage_tag[i][j];
    }
  }
  const int ftq_alloc_idx = ftq_alloc();
  if (ftq_alloc_idx < 0) {
    out.pre2front->ready = false;
    return;
  }
  ftq_entries_1[ftq_alloc_idx] = ftq_entry;
  ftq_entries_1[ftq_alloc_idx].valid = true;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.pre2front->fire[i] = in.front2pre->valid[i];
  }

  int last_fire_idx = -1;
  for (int i = FETCH_WIDTH - 1; i >= 0; i--) {
    if (out.pre2front->fire[i]) {
      last_fire_idx = i;
      break;
    }
  }
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (!out.pre2front->fire[i]) {
      continue;
    }
    auto &e = push_entries[push_count++];
    e.valid = true;
    e.inst = in.front2pre->inst[i];
    e.pc = in.front2pre->pc[i];
    e.page_fault_inst = in.front2pre->page_fault_inst[i];
    e.ftq_idx = ftq_alloc_idx;
    e.ftq_offset = i;
    e.ftq_is_last = (i == last_fire_idx);
  }

  if (ctx != nullptr && push_count > 0) {
    ctx->perf.ib_write_cycle_total++;
    ctx->perf.ib_write_inst_total += static_cast<uint64_t>(push_count);
  }
}

/*
 * comb_fire
 * 功能: 统一处理消费统计、flush/recover、IBUF 下一拍状态计算与 FTQ 提交回收。
 * 输入依赖: in.idu_consume->fire[], in.rob_bcast->flush,
 * in.idu_br_latch->{mispred,ftq_idx}, in.rob_commit->commit_entry[],
 * push_entries/push_count。 输出更新: ibuf_1,
 * ftq_head_1/ftq_tail_1/ftq_count_1, ftq_entries_1。 约束: flush 优先于
 * recover；flush/recover 生效时跳过 commit reclaim；IBUF 在 flush/mispred
 * 时清空。
 */
void PreIduQueue::comb_fire() {
  int pop_count = 0;
  bool ftq_flush_req = false;
  bool ftq_recover_req = false;
  int ftq_recover_tail = 0;

  // 1) consume 统计
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (in.idu_consume->fire[i]) {
      pop_count++;
    } else {
      break;
    }
  }
#ifdef CONFIG_PERF_COUNTER
  if (ctx != nullptr) {
    ctx->perf.ib_consume_consumed_slots += static_cast<uint64_t>(pop_count);
  }
#endif

  // 2) flush/recover 决策与 FTQ 更新
  if (in.rob_bcast->flush) {
    ftq_flush_req = true;
  } else if (in.idu_br_latch->mispred) {
    ftq_recover_req = true;
    ftq_recover_tail = (in.idu_br_latch->ftq_idx + 1) % FTQ_SIZE;
  }

  if (ftq_flush_req) {
    ftq_flush();
  } else if (ftq_recover_req) {
    ftq_recover(ftq_recover_tail);
  }

  // IBUF 下一拍状态计算（seq 只做提交）
  if (in.rob_bcast->flush) {
    ibuf_1.clear();
  } else if (in.idu_br_latch->mispred) {
    ibuf_1.clear();
  } else {
    if (pop_count > 0) {
      ibuf_1.pop_front(pop_count);
    }
    if (push_count > 0) {
      for (int i = 0; i < push_count; i++) {
        ibuf_1.push_back(push_entries[i]);
      }
    }
  }

  // 3) FTQ 提交回收
  if (ftq_flush_req || ftq_recover_req) {
    return;
  }
  int pop_cnt = 0;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid &&
        in.rob_commit->commit_entry[i].uop.ftq_is_last) {
      pop_cnt++;
    }
  }
  ftq_pop(pop_cnt);
}

/*
 * comb_ftq_lookup
 * 功能: 响应 EXU/ROB 的 FTQ PC 读请求，返回 entry 命中信息与重建
 * PC/next_pc/pred_taken。 输入依赖: in.ftq_exu_pc_req->req[],
 * in.ftq_rob_pc_req->req[], ftq_entries[]。 输出更新:
 * out.ftq_exu_pc_resp->resp[], out.ftq_rob_pc_resp->resp[]。 约束: 仅对 valid
 * 请求生成有效响应；无请求槽位输出清零默认值。
 */
void PreIduQueue::comb_ftq_lookup() {
  for (int i = 0; i < FTQ_EXU_PC_PORT_NUM; i++) {
    out.ftq_exu_pc_resp->resp[i] = {};
    if (in.ftq_exu_pc_req->req[i].valid) {
      fill_ftq_pc_resp(out.ftq_exu_pc_resp->resp[i],
                       ftq_entries[in.ftq_exu_pc_req->req[i].ftq_idx],
                       in.ftq_exu_pc_req->req[i]);
    }
  }

  for (int i = 0; i < FTQ_ROB_PC_PORT_NUM; i++) {
    out.ftq_rob_pc_resp->resp[i] = {};
    if (in.ftq_rob_pc_req->req[i].valid) {
      fill_ftq_pc_resp(out.ftq_rob_pc_resp->resp[i],
                       ftq_entries[in.ftq_rob_pc_req->req[i].ftq_idx],
                       in.ftq_rob_pc_req->req[i]);
    }
  }
}

void PreIduQueue::seq() {
  ibuf = ibuf_1;
  ftq_head = ftq_head_1;
  ftq_tail = ftq_tail_1;
  ftq_count = ftq_count_1;
  std::memcpy(ftq_entries, ftq_entries_1, sizeof(ftq_entries));
}

const FTQEntry *PreIduQueue::lookup_ftq_entry(uint32_t idx) const {
  if (idx >= FTQ_SIZE) {
    return nullptr;
  }
  return &ftq_entries[idx];
}
