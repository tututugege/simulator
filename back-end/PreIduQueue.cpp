#include "PreIduQueue.h"
#include "types.h"

#include <cstring>

static InstructionBufferEntry push_entries[FETCH_WIDTH];
static int push_count = 0;

static void fill_ftq_pc_resp(FtqPcReadResp &resp, const FTQEntry &entry,
                             wire<1> entry_valid, const FtqPcReadReq &req) {
  resp = {};
  if (!req.valid) {
    return;
  }

  resp.valid = true;
  resp.entry_valid = entry_valid;
  resp.pc = entry.slot_pc[req.ftq_offset];
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
    ftq_valid_1[ftq_head_1] = false;
    ftq_head_1 = (ftq_head_1 + 1) % FTQ_SIZE;
    ftq_count_1--;
  }
}

void PreIduQueue::ftq_recover(int new_tail) {
  int normalized_tail = ((new_tail % FTQ_SIZE) + FTQ_SIZE) % FTQ_SIZE;
  int discarded = (ftq_tail_1 - normalized_tail + FTQ_SIZE) % FTQ_SIZE;
  for (int i = 0; i < discarded; i++) {
    int idx = (normalized_tail + i) % FTQ_SIZE;
    ftq_valid_1[idx] = false;
  }
  ftq_tail_1 = normalized_tail;
  ftq_count_1 -= discarded;
}

void PreIduQueue::ftq_flush() {
  ftq_head_1 = 0;
  ftq_tail_1 = 0;
  ftq_count_1 = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_valid_1[i] = false;
  }
}

void PreIduQueue::init() {
  ibuf.init();
  ibuf_1.init();
  ftq_head = ftq_tail = ftq_count = 0;
  ftq_head_1 = ftq_tail_1 = ftq_count_1 = 0;
  push_count = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_lookup_entries[i] = FTQEntry();
    ftq_lookup_entries_1[i] = FTQEntry();
    ftq_train_meta_fifo[i] = FTQTrainMetaEntry();
    ftq_train_meta_fifo_1[i] = FTQTrainMetaEntry();
    ftq_valid[i] = false;
    ftq_valid_1[i] = false;
  }
}

void PreIduQueue::comb_begin() {
  ibuf_1 = ibuf;
  ftq_head_1 = ftq_head;
  ftq_tail_1 = ftq_tail;
  ftq_count_1 = ftq_count;
  std::memcpy(ftq_lookup_entries_1, ftq_lookup_entries, sizeof(ftq_lookup_entries));
  std::memcpy(ftq_train_meta_fifo_1, ftq_train_meta_fifo, sizeof(ftq_train_meta_fifo));
  std::memcpy(ftq_valid_1, ftq_valid, sizeof(ftq_valid));

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

  FTQEntry ftq_lookup_entry;
  FTQTrainMetaEntry ftq_train_meta_entry;
  ftq_lookup_entry.next_pc = in.front2pre->predict_next_fetch_address[0];
  for (int i = 0; i < FETCH_WIDTH; i++) {
    ftq_lookup_entry.slot_pc[i] = in.front2pre->pc[i];
    ftq_lookup_entry.pred_taken_mask[i] = in.front2pre->predict_dir[i];
    ftq_train_meta_entry.alt_pred[i] = in.front2pre->alt_pred[i];
    ftq_train_meta_entry.altpcpn[i] = in.front2pre->altpcpn[i];
    ftq_train_meta_entry.pcpn[i] = in.front2pre->pcpn[i];
    ftq_train_meta_entry.sc_used[i] = in.front2pre->sc_used[i];
    ftq_train_meta_entry.sc_pred[i] = in.front2pre->sc_pred[i];
    ftq_train_meta_entry.sc_sum[i] = in.front2pre->sc_sum[i];
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      ftq_train_meta_entry.sc_idx[i][t] = in.front2pre->sc_idx[i][t];
    }
    ftq_train_meta_entry.loop_used[i] = in.front2pre->loop_used[i];
    ftq_train_meta_entry.loop_hit[i] = in.front2pre->loop_hit[i];
    ftq_train_meta_entry.loop_pred[i] = in.front2pre->loop_pred[i];
    ftq_train_meta_entry.loop_idx[i] = in.front2pre->loop_idx[i];
    ftq_train_meta_entry.loop_tag[i] = in.front2pre->loop_tag[i];
    for (int j = 0; j < 4; j++) {
      ftq_train_meta_entry.tage_idx[i][j] = in.front2pre->tage_idx[i][j];
      ftq_train_meta_entry.tage_tag[i][j] = in.front2pre->tage_tag[i][j];
    }
  }
  const int ftq_alloc_idx = ftq_alloc();
  if (ftq_alloc_idx < 0) {
    out.pre2front->ready = false;
    return;
  }
  ftq_lookup_entries_1[ftq_alloc_idx] = ftq_lookup_entry;
  ftq_train_meta_fifo_1[ftq_alloc_idx] = ftq_train_meta_entry;
  ftq_valid_1[ftq_alloc_idx] = true;

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

void PreIduQueue::comb_fire() {
  int pop_count = 0;
  bool ftq_flush_req = false;
  bool ftq_recover_req = false;
  int ftq_recover_tail = 0;

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

void PreIduQueue::comb_ftq_lookup() {
  for (int i = 0; i < FTQ_EXU_PC_PORT_NUM; i++) {
    out.ftq_exu_pc_resp->resp[i] = {};
    if (in.ftq_exu_pc_req->req[i].valid) {
      fill_ftq_pc_resp(out.ftq_exu_pc_resp->resp[i],
                       ftq_lookup_entries[in.ftq_exu_pc_req->req[i].ftq_idx],
                       ftq_valid[in.ftq_exu_pc_req->req[i].ftq_idx],
                       in.ftq_exu_pc_req->req[i]);
    }
  }

  for (int i = 0; i < FTQ_ROB_PC_PORT_NUM; i++) {
    out.ftq_rob_pc_resp->resp[i] = {};
    if (in.ftq_rob_pc_req->req[i].valid) {
      fill_ftq_pc_resp(out.ftq_rob_pc_resp->resp[i],
                       ftq_lookup_entries[in.ftq_rob_pc_req->req[i].ftq_idx],
                       ftq_valid[in.ftq_rob_pc_req->req[i].ftq_idx],
                       in.ftq_rob_pc_req->req[i]);
    }
  }
}

void PreIduQueue::seq() {
  ibuf = ibuf_1;
  ftq_head = ftq_head_1;
  ftq_tail = ftq_tail_1;
  ftq_count = ftq_count_1;
  std::memcpy(ftq_lookup_entries, ftq_lookup_entries_1, sizeof(ftq_lookup_entries));
  std::memcpy(ftq_train_meta_fifo, ftq_train_meta_fifo_1, sizeof(ftq_train_meta_fifo));
  std::memcpy(ftq_valid, ftq_valid_1, sizeof(ftq_valid));
}

const FTQEntry *PreIduQueue::lookup_ftq_entry(uint32_t idx) const {
  if (idx >= FTQ_SIZE) {
    return nullptr;
  }
  if (!ftq_valid[idx]) {
    return nullptr;
  }
  return &ftq_lookup_entries[idx];
}

bool PreIduQueue::ftq_train_meta_cursor_begin(uint32_t &cursor_idx) const {
  if (ftq_count == 0) {
    return false;
  }
  cursor_idx = static_cast<uint32_t>(ftq_head);
  return true;
}

const FTQTrainMetaEntry *
PreIduQueue::ftq_train_meta_cursor_peek(uint32_t cursor_idx) const {
  if (cursor_idx >= FTQ_SIZE) {
    return nullptr;
  }
  if (!ftq_valid[cursor_idx]) {
    return nullptr;
  }
  return &ftq_train_meta_fifo[cursor_idx];
}

bool PreIduQueue::ftq_train_meta_cursor_advance(uint32_t &cursor_idx) const {
  if (ftq_count == 0) {
    return false;
  }
  uint32_t next = (cursor_idx + 1) % FTQ_SIZE;
  if (next == static_cast<uint32_t>(ftq_tail)) {
    return false;
  }
  cursor_idx = next;
  return true;
}
