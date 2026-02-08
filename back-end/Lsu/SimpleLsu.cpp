#include "SimpleLsu.h"
#include "AbstractLsu.h"
#include "config.h"
#include "oracle.h"
#include "util.h"
#include <cstdint>
#include <cstring>

// 外部辅助函数声明
extern uint32_t *p_memory;

SimpleLsu::SimpleLsu(SimContext *ctx) : AbstractLsu(ctx), cache(ctx) {
  // Initialize MMU
  mmu = new SimpleMmu(ctx, this);

  stq_head = 0;
  stq_tail = 0;
  stq_commit = 0;
  stq_count = 0;

  // 初始化所有 STQ 条目，防止未初始化内存导致的破坏
  for (int i = 0; i < STQ_NUM; i++) {
    stq[i].valid = false;
    stq[i].addr_valid = false;
    stq[i].data_valid = false;
    stq[i].committed = false;
    stq[i].addr = 0;
    stq[i].data = 0;
    stq[i].tag = 0;
    stq[i].rob_idx = 0;
    stq[i].rob_flag = 0;
    stq[i].func3 = 0;
  }
}

void SimpleLsu::init() {}

// =========================================================
// 1. Dispatch 阶段: STQ 分配反馈
// =========================================================

void SimpleLsu::comb_lsu2dis_info() {
  // 这里的逻辑很简单：只读当前状态，绝不写 next_ 状态
  // 就像我在微信上告诉你：“土土，今晚有空。”（我还没决定去哪）
  out.lsu2dis->stq_tail = this->stq_tail;

  // 注意：这里的 count 必须是当前周期的准确值
  out.lsu2dis->stq_free = STQ_NUM - this->stq_count;
  out.lsu2dis->ldq_free = MAX_INFLIGHT_LOADS - this->inflight_loads.size();
}

void SimpleLsu::comb_stq_alloc() {
  // 计算分配增量：遍历所有可能的请求端口
  int alloc_count = 0;
  for (int i = 0; i < MAX_STQ_DISPATCH_WIDTH; i++) {
    if (in.dis2lsu->alloc_req[i])
      alloc_count++;
  }

  next_stq_tail = (this->stq_tail + alloc_count) % STQ_NUM;
}

// =========================================================
// 2. Execute 阶段: 接收 AGU/SDU 请求 (多端口轮询)
// =========================================================
void SimpleLsu::comb_recv() {
  // 1. 优先级：Store Data (来自 SDU)
  // 确保在消费者检查之前数据就绪
  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    if (in.exe2lsu->sdu_req[i].valid) {
      handle_store_data(in.exe2lsu->sdu_req[i].uop);
    }
  }

  // 2. 优先级：Store Addr (来自 AGU)
  // 确保地址对于别名检查有效
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_STA) {
        handle_store_addr(uop);
      }
    }
  }

  // 3. 优先级：Loads (来自 AGU)
  // 最后处理 Load，使其能看到本周期最新的 Store (STLF)
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_LOAD) {
        handle_load_req(uop);
      }
    }
  }
}

// =========================================================
// 3. Writeback 阶段: 输出 Load 结果 (多端口写回)
// =========================================================
void SimpleLsu::comb_load_res() {
  // 1. 先清空所有写回端口
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.lsu2exe->wb_req[i].valid = false;
  }

  // 2. 从完成队列填充端口 (Load)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (!finished_loads.empty()) {
      out.lsu2exe->wb_req[i].valid = true;
      out.lsu2exe->wb_req[i].uop = finished_loads.front();

      finished_loads.pop_front();
    } else {
      break;
    }
  }

  // 3. 从完成队列填充端口 (STA)
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (!finished_sta_reqs.empty()) {
      out.lsu2exe->sta_wb_req[i].valid = true;
      out.lsu2exe->sta_wb_req[i].uop = finished_sta_reqs.front();
      finished_sta_reqs.pop_front();
    } else {
      out.lsu2exe->sta_wb_req[i].valid = false;
    }
  }
}

// 内部辅助: 启动 Load 流程 (原 dispatch_load)
void SimpleLsu::handle_load_req(const InstUop &inst) {
  // 注意：这里是组合逻辑，不能直接修改 inflight_loads (这是 seq 的状态)
  // 但为了简化代码，我们假设这里是一个 "Next State Logic"，或者有一个 input
  // latch 严格的硬件模拟应该把 task 放入一个 new_tasks 列表，在 seq 里 merge

  // 这里采用简化做法：直接操作 inflight_loads，但在 seq 里处理时间推进
  // 只要 inflight_loads 不被当作寄存器输出回环即可

  InstUop task = inst;
  uint32_t p_addr;
  bool ret = mmu->translate(p_addr, task.result, 1, in.csr_status);

  if (!ret) {
    task.page_fault_load = true;
    task.cplt_time = sim_time + 1;
  } else {
    task.paddr = p_addr;

    // [Fix] Disable Store-to-Load Forwarding for MMIO ranges
    // These addresses involve side effects and must read from consistent memory
    bool is_mmio = ((p_addr & UART_ADDR_MASK) == UART_ADDR_BASE) ||
                   ((p_addr & PLIC_ADDR_MASK) == PLIC_ADDR_BASE);

    task.flush_pipe = is_mmio;
    auto fwd_res =
        is_mmio ? std::make_pair(0, 0u) : check_store_forward(p_addr, inst);

    if (fwd_res.first == 1) {
      // 这里的 Store 给了我们数据！不用查缓存了！
      // 这就是所谓的“Store-to-Load Forwarding Latency” (通常很短，0 或 1)
      task.result = fwd_res.second;
      task.cplt_time = sim_time + 0; // 这一拍直接完成！

      // 注意：如果是 Stall 逻辑 (Store 地址匹配但数据未就绪)，
      // 这里的 check_store_forwarding 应该返回特殊状态，或者在这里不做
      // push_back
    } else if (fwd_res.first == 0) {
      // ❌ STQ 里没有，去读内存
      // 模拟 Cache 访问
      int latency = cache.cache_access(p_addr);
      task.cplt_time = sim_time + latency;
      uint32_t mem_val = p_memory[p_addr >> 2];

      // Simple MMIO Read Interception
      // Sync with Oracle's timer to prevent execution divergence
      if (p_addr == 0x1fd0e000) {
#ifdef CONFIG_BPU
        mem_val = sim_time;
#else
        mem_val = get_oracle_timer();
#endif
        task.difftest_skip = true;
      } else if (p_addr == 0x1fd0e004) {
        mem_val = 0;
        task.difftest_skip = true;
      } else {
        // Normal Memory Access (or Garbage). DO NOT SKIP.
        // Let Difftest catch divergence.
        task.difftest_skip = false;
      }

      task.result = extract_data(mem_val, p_addr, inst.func3);
    } else {
      // 🔄 [Retry] Store 地址匹配但数据未就绪 (Stall)
      // 设置特殊完成时间，让 seq 中的逻辑不断重试
      task.cplt_time = 0x7FFFFFFFFFFFFFFF; // LLONG_MAX
    }
  }

  // 标记为新进入的 load，seq 中会统一处理
  inflight_loads.push_back(task);
}

void SimpleLsu::handle_store_addr(const InstUop &inst) {
  int idx = inst.stq_idx;
  stq[idx].addr = inst.result; // VA
  // Translate VA -> PA
  uint32_t pa = inst.result;
  bool ret = mmu->translate(pa, inst.result, 2, in.csr_status); // 2=Store

  if (!ret) {
    // ⚠️ Store Page Fault Detected!
    // Report to ROB via Writeback/Exception path
    InstUop fault_op = inst;
    fault_op.page_fault_store = true;
    fault_op.cplt_time = sim_time; // Immediate failure

    // Store address calculation completed (with exception)
    finished_sta_reqs.push_back(fault_op);
  } else {
    // Normal STA completion (Optional: we could also return it through Port 5
    // to ensure ROB only considers it complete AFTER MMU translation.
    // Given the user's advice, we will let ALL STA results go through Port 5
    // via LSU.)
    InstUop success_op = inst;
    success_op.cplt_time = sim_time;
    bool is_mmio = ((pa & UART_ADDR_MASK) == UART_ADDR_BASE) ||
                   ((pa & PLIC_ADDR_MASK) == PLIC_ADDR_BASE);
    success_op.flush_pipe = is_mmio;
    finished_sta_reqs.push_back(success_op);
  }

  stq[idx].p_addr = pa;
  stq[idx].addr_valid = true;
}

void SimpleLsu::handle_store_data(const InstUop &inst) {
  stq[inst.stq_idx].data = inst.result;
  stq[inst.stq_idx].data_valid = true;
}

// =========================================================
// 4. Commit 阶段: 提交 Store
// =========================================================
void SimpleLsu::comb_commit() {
  next_stq_commit = this->stq_commit;

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid &&
        is_store(in.rob_commit->commit_entry[i].uop)) {
      int idx = in.rob_commit->commit_entry[i].uop.stq_idx;
      stq[idx].committed = true;
      Assert(this->next_stq_commit == idx);
      next_stq_commit = (this->next_stq_commit + 1) % STQ_NUM;
    }
  }
}

// =========================================================
// 5. Exception: Flush 处理
// =========================================================

void SimpleLsu::comb_flush() {
  if (in.rob_bcast->flush) {
    // 1. 清空飞行中的 Load
    inflight_loads.clear();
    finished_loads.clear();
    finished_sta_reqs.clear();

    // 2. STQ 回滚: Tail -> Commit
    // 丢弃所有投机状态，只保留已提交状态
    next_stq_tail = stq_commit;
  }
}

// =========================================================
// 6. Sequential Logic: 状态更新与时序模拟
// =========================================================
void SimpleLsu::seq() {
  // === Step 1: 准备变量 ===
  bool is_flush = in.rob_bcast->flush;
  bool is_mispred = in.dec_bcast->mispred;

  // 临时变量，用于计算本周期的变化量
  int push_count = 0; // 进队数量 (Dispatch)
  int pop_count = 0;  // 出队数量 (Writeback/Retire)

  // === Step 2: 处理 Flush / Mispred (优先级最高) ===

  if (is_flush) {
    // 全局冲刷：直接应用 flush 逻辑算好的值
    int old_tail = stq_tail;
    stq_tail = stq_commit; // 回滚到非投机点
    // stq_head 保持不变 (已提交的不能扔)

    // 重新计算 Count (信赖指针差值，因为 Flush 后不会满)
    stq_count = (stq_tail - stq_head + STQ_NUM) % STQ_NUM;

    // 修正：明确清除冲刷掉的条目以防止“僵尸”条目
    int ptr = stq_tail;
    while (ptr != old_tail) {
      stq[ptr].valid = false;
      stq[ptr].addr_valid = false;
      stq[ptr].data_valid = false;
      ptr = (ptr + 1) % STQ_NUM;
    }

    return; // ⛔ Flush 这一拍不处理正常的进出队，直接返回
  }

  if (is_mispred) {
    uint64_t mask = in.dec_bcast->br_mask;
    // 清除 inflight_loads 中被 Squash 的指令
    auto it_inflight = inflight_loads.begin();
    while (it_inflight != inflight_loads.end()) {
      if ((1ULL << it_inflight->tag) & mask) {
        it_inflight = inflight_loads.erase(it_inflight);
      } else {
        ++it_inflight;
      }
    }

    // 清除 finished_sta_reqs 中被 Squash 的指令
    auto it_sta = finished_sta_reqs.begin();
    while (it_sta != finished_sta_reqs.end()) {
      if ((1ULL << it_sta->tag) & mask) {
        it_sta = finished_sta_reqs.erase(it_sta);
      } else {
        ++it_sta;
      }
    }

    // [Fix] Also clear finished_loads that are waiting for WB
    auto it_finished = finished_loads.begin();
    while (it_finished != finished_loads.end()) {
      if ((1ULL << it_finished->tag) & mask) {
        it_finished = finished_loads.erase(it_finished);
      } else {
        ++it_finished;
      }
    }

    // 分支误预测：Tail 回滚到某个中间点
    int recovery_tail = find_recovery_tail(mask);

    if (recovery_tail != -1) {
      int old_tail = stq_tail;
      stq_tail = recovery_tail;
      // 重新计算 Count
      stq_count = (stq_tail - stq_head + STQ_NUM) % STQ_NUM;

      // 修正：明确清除冲刷掉的条目
      int ptr = stq_tail;

      if (old_tail == stq_tail) {
        // 特殊情况：将满队列回滚到相同的指针
        do {
          stq[ptr].valid = false;
          stq[ptr].addr_valid = false;
          stq[ptr].data_valid = false;
          ptr = (ptr + 1) % STQ_NUM;
        } while (ptr != old_tail);
      } else {
        while (ptr != old_tail) {
          stq[ptr].valid = false;
          stq[ptr].addr_valid = false;
          stq[ptr].data_valid = false;
          ptr = (ptr + 1) % STQ_NUM;
        }
      }
    }

    // 关键修正：即使没有回滚 (recovery_tail == stq_tail)，
    // 我们在此误预测周期内也绝对不能接受新的分配！
    // 此时本周期分派的指令肯定是在错误路径上的。
    return;
  }

  // === Step 3: 正常的入队逻辑 (Alloc) ===
  // 只有没满的时候才允许入队 (Dispatch 阶段保证了 lsu2dis->stq_free > 0)

  for (int i = 0; i < MAX_STQ_DISPATCH_WIDTH; i++) {
    if (in.dis2lsu->alloc_req[i]) {
      // 1. 写入 Payload
      stq[stq_tail].valid = true;
      stq[stq_tail].addr_valid = false;
      stq[stq_tail].data_valid = false;
      stq[stq_tail].committed = false;

      stq[stq_tail].tag = in.dis2lsu->tag[i];
      stq[stq_tail].rob_idx = in.dis2lsu->rob_idx[i];
      stq[stq_tail].rob_flag = in.dis2lsu->rob_flag[i];
      stq[stq_tail].func3 = in.dis2lsu->func3[i];

      // 2. 移动 Tail
      stq_tail = (stq_tail + 1) % STQ_NUM;

      // 3. 累加进队计数
      push_count++;
    }
  }

  // === Step 4: 正常的出队逻辑 (Retire/Writeback) ===
  // 只要 Head != Commit，说明有东西已经 Commit 了，可以写内存
  // [Note] 保持单端口提交以隔离 coherent_read 修复

  if (stq_head != stq_commit) {
    StqEntry &head = stq[stq_head];

    // 只有当这一项完全 Ready 时才出队
    if (head.valid && head.addr_valid && head.data_valid) {
      // 1. 写内存 (Memory Access)
      cache.cache_access(head.p_addr);
      uint32_t paddr = head.p_addr;
      uint32_t word_idx = paddr >> 2;
      uint32_t old_val = p_memory[word_idx];
      uint32_t new_val =
          merge_data_to_word(old_val, head.data, paddr, head.func3);
      p_memory[word_idx] = new_val;

      // Simple MMIO Write Side Effect
      if (paddr == UART_ADDR_BASE) {
        char temp = new_val & 0xFF;
        std::cout << temp << std::flush;
        p_memory[word_idx] &= 0xFFFFFF00;
      } else if (paddr == UART_ADDR_BASE + 1) {
        uint8_t cmd = head.data & 0xff;
        if (cmd == 7) {
          p_memory[PLIC_CLAIM_ADDR / 4] = 0xa;
          p_memory[UART_ADDR_BASE / 4] &= 0xfff0ffff;
        } else if (cmd == 5) {
          p_memory[UART_ADDR_BASE / 4] =
              (p_memory[UART_ADDR_BASE / 4] & 0xfff0ffff) | 0x00030000;
        }
      } else if (paddr == PLIC_CLAIM_ADDR) {
        if ((head.data & 0xff) == 0xa) {
          p_memory[PLIC_CLAIM_ADDR / 4] = 0x0;
        }
      }

      // 2. 清理条目
      head.valid = false;
      head.committed = false;
      head.addr_valid = false;
      head.data_valid = false;
      head.addr = 0;
      head.data = 0;

      // 3. 移动 Head
      stq_head = (stq_head + 1) % STQ_NUM;

      // 4. 累加出队计数
      pop_count++;
    }
  }

  // === Step 5: 更新 Commit 指针 (来自 ROB) ===
  // Commit 指针只是在 Ring Buffer 里向后滑动，标记 "安全线"
  // 它不改变 Count (Count 是 Head 到 Tail 的总长度)

  // 复用你之前的逻辑，但是要小心不要越界
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid &&
        is_store(in.rob_commit->commit_entry[i].uop)) {

      int idx = in.rob_commit->commit_entry[i].uop.stq_idx;

      // 简单校验
      if (idx == stq_commit) {
        stq[idx].committed = true;
        stq_commit = (stq_commit + 1) % STQ_NUM;
      } else {
        // 应该 Assert，ROB 必须按顺序 Commit Store
        Assert(0 && "Store commit out of order?");
      }
    }
  }

  // === Step 6: 最终更新 Count (核心修复！) ✨ ===
  // 不要用 Head/Tail 重新算！直接加减！
  // 这样 16(满) + 0 - 0 = 16，不会变成 0。

  stq_count = stq_count + push_count - pop_count;

  // 🛡️ 安全检查 (防止逻辑错乱导致溢出)
  if (stq_count < 0) {
    Assert(0 && "STQ Count Underflow! logic bug!");
  }
  if (stq_count > STQ_NUM) {
    Assert(0 && "STQ Count Overflow! logic bug!");
  }

  // 处理 Load 队列的 Tick (包含 Replay 逻辑)
  auto it = inflight_loads.begin();
  while (it != inflight_loads.end()) {
    // 🔄 Replay Check: 如果任务处于等待状态 (cplt_time == LLONG_MAX)
    if (it->cplt_time == 0x7FFFFFFFFFFFFFFF) {
      auto fwd_res = check_store_forward(it->paddr, *it);
      if (fwd_res.first == 1) { // Hit -> Success
        it->result = fwd_res.second;
        it->cplt_time = sim_time;      // 完成
      } else if (fwd_res.first == 0) { // Miss -> Memory
        int lat = cache.cache_access(it->paddr);
        it->cplt_time = sim_time + lat;
        uint32_t mem_val = p_memory[it->paddr >> 2];
        it->result = extract_data(mem_val, it->paddr, it->func3);
      }
      // If 2 (Retry), keep waiting
    }

    if (it->cplt_time <= sim_time) {
      finished_loads.push_back(*it);
      it = inflight_loads.erase(it);
    } else {
      ++it;
    }
  }
}

// =========================================================
// 辅助：基于 Tag 查找新的 Tail
// =========================================================
int SimpleLsu::find_recovery_tail(mask_t br_mask) {
  // 从 Commit 指针（安全点）开始，向 Tail 扫描
  // 我们要找的是“第一个”被误预测影响的指令
  // 因为是顺序分配，一旦找到一个，后面（更年轻）的肯定也都要丢弃

  int ptr = stq_commit;

  // 修正：正确计算未提交指令数，处理队列已满的情况 (Tail == Commit)
  // stq_count 追踪总有效条目 (Head -> Tail)。
  // Head -> Commit 之间的条目已提交。
  // Commit -> Tail 之间的条目未提交。
  int committed_count = (stq_commit - stq_head + STQ_NUM) % STQ_NUM;
  int uncommitted_count = stq_count - committed_count;

  // 安全检查
  if (uncommitted_count < 0)
    uncommitted_count = 0; // 不应该发生
  int count = uncommitted_count;

  for (int i = 0; i < count; i++) {
    // 检查当前条目是否依赖于被误预测的分支
    if (stq[ptr].valid && ((1ULL << stq[ptr].tag) & br_mask)) {
      // 找到了！这个位置就是错误路径的开始
      // 新的 Tail 应该回滚到这里
      return ptr;
    }
    ptr = (ptr + 1) % STQ_NUM;
  }

  // 扫描完所有未提交指令都没找到相关依赖 -> 不需要回滚
  return -1;
}

bool SimpleLsu::is_store_older(int s_idx, int s_flag, int l_idx, int l_flag) {
  if (s_flag == l_flag) {
    return s_idx < l_idx;
  } else {
    return s_idx > l_idx;
  }
}

// =========================================================
// 🛡️ [Nanako Implementation] 完整的 STLF 模拟逻辑
// =========================================================
std::pair<int, uint32_t>
SimpleLsu::check_store_forward(uint32_t p_addr, const InstUop &load_uop) {

  // Load 的范围
  int load_width = get_mem_width(load_uop.func3);
  uint32_t load_start = p_addr;
  uint32_t load_end = p_addr + load_width;

  // 1. 【底板准备】直接从 Memory 读取数据作为 "默认背景" 🖼️
  // 即使后面 STQ 完全覆盖了它，读一次内存的开销在功能模拟器里也可以接受
  // 这样保证了那些没有被 Store 覆盖到的 "缝隙" 自动拥有了内存里的正确值

  uint8_t byte_buffer[8]; // 这里的 buffer 直接初始化为内存值

  for (int k = 0; k < load_width; k++) {
    uint32_t curr_addr = load_start + k;
    // 模拟字节粒度读取内存 (提取对应的 Byte)
    // 注意：这里假设 p_memory 是 uint32_t*，需要移位提取
    uint32_t mem_word = p_memory[curr_addr >> 2];
    int bit_offset = (curr_addr & 3) * 8;
    byte_buffer[k] = (mem_word >> bit_offset) & 0xFF;
  }

  // 标记是否命中了 STQ (虽然数据混合了，但如果没有命中任何 Store，
  // 逻辑上这不算是一次 Forwarding，而是普通的 Cache Access。
  // 不过为了简化，如果只想复用这个混合结果，这里其实可以忽略 hit_any，
  // 但为了模拟器的统计准确性，还是记录一下比较好)
  bool hit_any = false;

  // 2. 【涂层覆盖】正向遍历 STQ (Head -> Tail) 🖌️
  // 后面的 Store 会自动覆盖前面的 Store，也会覆盖底板 Memory

  int ptr = this->stq_head;
  int current_count = this->stq_count;

  for (int i = 0; i < current_count; i++) {
    StqEntry &entry = stq[ptr];

    // A. 基础有效性与年龄检查
    // 🛡️ CRITICAL FIX: 已提交的 Store 在程序顺序上一定比当前指令老
    bool is_older =
        entry.committed || is_store_older(entry.rob_idx, entry.rob_flag,
                                          load_uop.rob_idx, load_uop.rob_flag);

    if (entry.valid && is_older) {
      if (!entry.addr_valid) {
        return {
            2,
            0}; // 🛡️ CRITICAL FIX: Stall if an older store's address is unknown
      }

      // B. 区间重叠计算
      int store_width = get_mem_width(entry.func3);
      uint32_t s_start = entry.p_addr;
      uint32_t s_end = entry.p_addr + store_width;

      uint32_t overlap_start = (load_start > s_start) ? load_start : s_start;
      uint32_t overlap_end = (load_end < s_end) ? load_end : s_end;

      // C. 如果有重叠
      if (overlap_start < overlap_end) {
        hit_any = true;

        if (!entry.data_valid) {
          // 必须等待数据就绪
          // 返回 2 (Retry) 让 Handle Load Req / Seq 暂停处理
          return {2, 0};
        }

        // D. 【关键】直接覆盖底板！
        for (uint32_t addr = overlap_start; addr < overlap_end; addr++) {
          int s_offset = addr - s_start;    // Store 里的偏移
          int l_offset = addr - load_start; // Buffer 里的偏移

          // 提取 Store 的字节，无情地覆盖掉 Buffer 里的 Memory 值 (或旧 Store
          // 值)
          byte_buffer[l_offset] = (entry.data >> (s_offset * 8)) & 0xFF;
        }
      }
    }

    ptr = (ptr + 1) % STQ_NUM;
  }

  // 3. 打包结果
  // 如果 hit_any = false，说明完全没碰到 STQ，buffer 里就是纯 Memory 数据
  // 如果 hit_any = true，说明 buffer 里是 Memory + Store 的混合体 (完美
  // Forwarding)

  // 这里的策略取决于你是否想把 "完全没命中 STQ" 算作 Forwarding 成功
  // 通常：
  // - 如果完全没命中，返回 false，让外部走标准的 cache_access (为了统计 Cache
  // Miss/Hit)
  // - 如果命中了（哪怕只覆盖了 1 个字节），就返回 true，直接用这里拼好的数据

  if (!hit_any) {
    return {false, 0};
  }

  uint32_t final_data = 0;
  for (int k = 0; k < load_width; k++) {
    final_data |= ((uint32_t)byte_buffer[k] << (k * 8));
  }

  // 4. 符号扩展 (Sign Extension) 📐
  // 不需要 Offset 移动！只需要处理符号位！

  switch (load_uop.func3) {
  case 0x0: // LB (8-bit Signed)
    // 检查第 7 位，如果是 1，则高 24 位全填 1
    if (final_data & 0x80)
      final_data |= 0xFFFFFF00;
    break;

  case 0x1: // LH (16-bit Signed)
    // 检查第 15 位，如果是 1，则高 16 位全填 1
    if (final_data & 0x8000)
      final_data |= 0xFFFF0000;
    break;

    // LBU (0x4), LHU (0x5), LW (0x2) 不需要做任何事，
    // 因为 final_data 初始化为 0，高位天然是 0 (Zero Extended)
  }
  return {true, final_data};
}


uint32_t SimpleLsu::coherent_read(uint32_t p_addr) {
  // 1. 基准值：读物理内存
  uint32_t data = p_memory[p_addr >> 2];

  // 2. 遍历 STQ 进行覆盖 (Coherent Check)
  // 虽然 MMU walk 通常是 4 字节对齐的 Word 访问，
  // 但我们支持字节合并以应对所有潜在对齐情况。
  int ptr = stq_head;
  int count = stq_count;
  for (int i = 0; i < count; i++) {
    const auto &entry = stq[ptr];
    if (entry.valid && entry.addr_valid) {
      // 检查地址范围是否有重叠 (当前访存地址的核心 Word)
      if ((entry.p_addr & ~0x3) == (p_addr & ~0x3)) {
        // 使用现有的合并助手更新结果
        data = merge_data_to_word(data, entry.data, entry.p_addr, entry.func3);
      }
    }
    ptr = (ptr + 1) % STQ_NUM;
  }

  return data;
}
