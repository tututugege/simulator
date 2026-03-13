#include "PreIduQueue.h"
#include "RISCV.h"
#include "types.h"
#include "util.h"

int PreIduQueue::ftq_alloc(const FTQEntry &entry) {
  (void)entry;
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
    ftq_entries[idx] = FTQEntry();
  }
  ftq_tail_1 = normalized_tail;
  ftq_count_1 -= discarded;
}

void PreIduQueue::ftq_flush() {
  ftq_head_1 = 0;
  ftq_tail_1 = 0;
  ftq_count_1 = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_entries[i] = FTQEntry();
    if (out.ftq_lookup != nullptr) {
      out.ftq_lookup->entries[i] = ftq_entries[i];
    }
  }
}

void PreIduQueue::init() {
  ibuf.init();
  ftq_head = ftq_tail = ftq_count = 0;
  ftq_head_1 = ftq_tail_1 = ftq_count_1 = 0;
  ftq_flush_req = false;
  ftq_recover_req = false;
  ftq_recover_tail = 0;
  ftq_alloc_req_valid = false;
  ftq_alloc_req_entry = {};
  ftq_alloc_success = false;
  ftq_alloc_idx = -1;
  br_latch = {};
  front_accept = false;
  push_count = 0;
  pop_count = 0;
  for (int i = 0; i < FTQ_SIZE; i++) {
    ftq_entries[i] = FTQEntry();
    if (out.ftq_lookup != nullptr) {
      out.ftq_lookup->entries[i] = ftq_entries[i];
    }
  }
}

void PreIduQueue::comb_begin() {
  ftq_head_1 = ftq_head;
  ftq_tail_1 = ftq_tail;
  ftq_count_1 = ftq_count;
  if (out.ftq_lookup != nullptr) {
    for (int i = 0; i < FTQ_SIZE; i++) {
      out.ftq_lookup->entries[i] = ftq_entries[i];
    }
  }

  if (out.issue != nullptr) {
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
  }

  if (out.dec2front != nullptr) {
    out.dec2front->ready = false;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.dec2front->fire[i] = false;
    }
  }

  ftq_flush_req = false;
  ftq_recover_req = false;
  ftq_recover_tail = 0;
  ftq_alloc_req_valid = false;
  ftq_alloc_req_entry = {};
  ftq_alloc_success = false;
  ftq_alloc_idx = -1;
  front_accept = false;
  push_count = 0;
  pop_count = 0;
}

void PreIduQueue::comb_accept_front() {
  Assert(out.dec2front != nullptr && "PreIduQueue: dec2front is null");
  Assert(in.front2dec != nullptr && "PreIduQueue: front2dec is null");
  if (br_latch.mispred || (in.rob_bcast != nullptr && in.rob_bcast->flush)) {
    out.dec2front->ready = false;
    return;
  }

  int incoming_valid_num = 0;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    incoming_valid_num += in.front2dec->valid[i] ? 1 : 0;
  }

  bool ftq_ok = (ftq_count < FTQ_SIZE);
  bool ib_ok = ibuf.can_accept(incoming_valid_num);
  out.dec2front->ready = ftq_ok && ib_ok;
  if (ctx != nullptr && incoming_valid_num > 0 && !out.dec2front->ready) {
    if (!ib_ok) {
      ctx->perf.ib_blocked_cycles++;
    }
    if (!ftq_ok) {
      ctx->perf.ftq_blocked_cycles++;
    }
  }
  if (!out.dec2front->ready || incoming_valid_num == 0) {
    return;
  }

  FTQEntry ftq_entry;
  ftq_entry.start_pc = in.front2dec->pc[0];
  ftq_entry.next_pc = in.front2dec->predict_next_fetch_address[0];
  for (int i = 0; i < FETCH_WIDTH; i++) {
    ftq_entry.pred_taken_mask[i] = in.front2dec->predict_dir[i];
    ftq_entry.alt_pred[i] = in.front2dec->alt_pred[i];
    ftq_entry.altpcpn[i] = in.front2dec->altpcpn[i];
    ftq_entry.pcpn[i] = in.front2dec->pcpn[i];
    for (int j = 0; j < 4; j++) {
      ftq_entry.tage_idx[i][j] = in.front2dec->tage_idx[i][j];
      ftq_entry.tage_tag[i][j] = in.front2dec->tage_tag[i][j];
    }
  }
  ftq_alloc_req_valid = true;
  ftq_alloc_req_entry = ftq_entry;
  ftq_alloc_idx = ftq_alloc(ftq_alloc_req_entry);
  ftq_alloc_success = (ftq_alloc_idx >= 0);
  if (!ftq_alloc_success) {
    out.dec2front->ready = false;
    return;
  }

  front_accept = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.dec2front->fire[i] = in.front2dec->valid[i];
  }

  int last_fire_idx = -1;
  for (int i = FETCH_WIDTH - 1; i >= 0; i--) {
    if (out.dec2front->fire[i]) {
      last_fire_idx = i;
      break;
    }
  }
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (!out.dec2front->fire[i]) {
      continue;
    }
    auto &e = push_entries[push_count++];
    e.valid = true;
    e.inst = in.front2dec->inst[i];
    e.pc = in.front2dec->pc[i];
    e.page_fault_inst = in.front2dec->page_fault_inst[i];
    e.predict_dir = in.front2dec->predict_dir[i];
    e.alt_pred = in.front2dec->alt_pred[i];
    e.altpcpn = in.front2dec->altpcpn[i];
    e.pcpn = in.front2dec->pcpn[i];
    for (int j = 0; j < 4; j++) {
      e.tage_idx[j] = in.front2dec->tage_idx[i][j];
    }
    e.ftq_idx = ftq_alloc_idx;
    e.ftq_offset = i;
    e.ftq_is_last = (i == last_fire_idx);
  }

  if (ctx != nullptr && push_count > 0) {
    ctx->perf.ib_write_cycle_total++;
    ctx->perf.ib_write_inst_total += static_cast<uint64_t>(push_count);
  }
}

void PreIduQueue::comb_consume_issue() {
  if (in.idu_dec2ren == nullptr || in.ren2dec == nullptr) {
    return;
  }
  pop_count = 0;
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (in.idu_dec2ren->valid[i] && in.ren2dec->ready) {
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
}

void PreIduQueue::comb_flush_recover() {
  if (in.rob_bcast != nullptr && in.rob_bcast->flush) {
    ftq_flush_req = true;
  } else if (br_latch.mispred) {
    ftq_recover_req = true;
    ftq_recover_tail = (br_latch.ftq_idx + 1) % FTQ_SIZE;
  }

  if (ftq_flush_req) {
    ftq_flush();
  } else if (ftq_recover_req) {
    ftq_recover(ftq_recover_tail);
  }
}

void PreIduQueue::comb_commit_reclaim() {
  if (ftq_flush_req || ftq_recover_req || in.rob_commit == nullptr) {
    return;
  }
  int pop_cnt = 0;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid && in.rob_commit->commit_entry[i].uop.ftq_is_last) {
      pop_cnt++;
    }
  }
  ftq_pop(pop_cnt);
}

void PreIduQueue::seq() {
  if (in.rob_bcast != nullptr && in.rob_bcast->flush) {
    ibuf.clear();
  } else if (br_latch.mispred) {
    ibuf.clear();
  } else {
    if (pop_count > 0) {
      ibuf.pop_front(pop_count);
    }
    if (front_accept) {
      for (int i = 0; i < push_count; i++) {
        ibuf.push_back(push_entries[i]);
      }
    }
  }

  if (in.rob_bcast != nullptr && in.rob_bcast->flush) {
    br_latch = {};
  } else if (in.exu2id != nullptr) {
    br_latch.mispred = in.exu2id->mispred;
    br_latch.ftq_idx = in.exu2id->ftq_idx;
  }

  ftq_head = ftq_head_1;
  ftq_tail = ftq_tail_1;
  ftq_count = ftq_count_1;
  if (ftq_alloc_success) {
    ftq_entries[ftq_alloc_idx] = ftq_alloc_req_entry;
    ftq_entries[ftq_alloc_idx].valid = true;
    if (out.ftq_lookup != nullptr) {
      out.ftq_lookup->entries[ftq_alloc_idx] = ftq_entries[ftq_alloc_idx];
    }
  }
}
