#pragma once

#include "config.h"
#include "IO.h"
#include "util.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

// 定义每个issue port操作类型的位掩码

// ==========================================
// 1. 配置结构体
// ==========================================
//
struct PortBinding {
  int port_idx;             // 物理端口号 (对应 out.iss2prf 下标)
  uint64_t capability_mask; // 该端口支持的操作码掩码
};

struct IssueQueueConfig {
  int id;                         // IQ 枚举 (IQ_INT, IQ_LD...)
  int size;                       // 深度
  int dispatch_width;             // 每周期最大入队数 (Write Ports)
  uint64_t supported_ops;         // 支持的操作码掩码
  std::vector<PortBinding> ports; // 存储端口配置
};

struct IqStoredUop {
  wire<PRF_IDX_WIDTH> dest_preg;
  wire<PRF_IDX_WIDTH> src1_preg;
  wire<PRF_IDX_WIDTH> src2_preg;

  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> is_atomic;
  wire<1> dest_en;
  wire<1> src1_en;
  wire<1> src2_en;
  wire<1> src1_busy;
  wire<1> src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm;
  wire<BR_TAG_WIDTH> br_id;
  wire<BR_MASK_WIDTH> br_mask;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<1> stq_flag;
  wire<LDQ_IDX_WIDTH> ldq_idx;

  wire<1> rob_flag;

  wire<UOP_TYPE_WIDTH> op;
  DebugMeta dbg;

  IqStoredUop() { std::memset(this, 0, sizeof(IqStoredUop)); }

  static IqStoredUop from_dis_iss_uop(const DisIssIO::DisIssUop &src) {
    IqStoredUop dst;
    dst.dest_preg = src.dest_preg;
    dst.src1_preg = src.src1_preg;
    dst.src2_preg = src.src2_preg;
    dst.ftq_idx = src.ftq_idx;
    dst.ftq_offset = src.ftq_offset;
    dst.is_atomic = src.is_atomic;
    dst.dest_en = src.dest_en;
    dst.src1_en = src.src1_en;
    dst.src2_en = src.src2_en;
    dst.src1_busy = src.src1_busy;
    dst.src2_busy = src.src2_busy;
    dst.src1_is_pc = src.src1_is_pc;
    dst.src2_is_imm = src.src2_is_imm;
    dst.func3 = src.func3;
    dst.func7 = src.func7;
    dst.imm = src.imm;
    dst.br_id = src.br_id;
    dst.br_mask = src.br_mask;
    dst.csr_idx = src.csr_idx;
    dst.rob_idx = src.rob_idx;
    dst.stq_idx = src.stq_idx;
    dst.stq_flag = src.stq_flag;
    dst.ldq_idx = src.ldq_idx;
    dst.rob_flag = src.rob_flag;
    dst.op = src.op;
    dst.dbg = src.dbg;
    return dst;
  }

  IssPrfIO::IssPrfUop to_iss_prf_uop() const {
    IssPrfIO::IssPrfUop dst;
    dst.dest_preg = dest_preg;
    dst.src1_preg = src1_preg;
    dst.src2_preg = src2_preg;
    dst.ftq_idx = ftq_idx;
    dst.ftq_offset = ftq_offset;
    dst.is_atomic = is_atomic;
    dst.dest_en = dest_en;
    dst.src1_en = src1_en;
    dst.src2_en = src2_en;
    dst.src1_is_pc = src1_is_pc;
    dst.src2_is_imm = src2_is_imm;
    dst.func3 = func3;
    dst.func7 = func7;
    dst.imm = imm;
    dst.br_id = br_id;
    dst.br_mask = br_mask;
    dst.csr_idx = csr_idx;
    dst.rob_idx = rob_idx;
    dst.stq_idx = stq_idx;
    dst.stq_flag = stq_flag;
    dst.ldq_idx = ldq_idx;
    dst.rob_flag = rob_flag;
    dst.op = op;
    dst.dbg = dbg;
    return dst;
  }
};

struct IqStoredEntry {
  wire<1> valid;
  IqStoredUop uop;
};

struct IssueQueueIn {
  std::vector<IqStoredEntry> enq_reqs;
  uint32_t wake_pregs[MAX_WAKEUP_PORTS] = {};
  wire<1> wake_valid[MAX_WAKEUP_PORTS] = {};
  wire<1> issue_block = 0;
  wire<1> flush_all = 0;
  wire<1> flush_br = 0;
  wire<BR_MASK_WIDTH> flush_br_mask = 0;
  wire<BR_MASK_WIDTH> clear_mask = 0;
  wire<1> port_ready[ISSUE_WIDTH] = {};
  wire<MAX_UOP_TYPE> port_fu_ready_mask[ISSUE_WIDTH] = {};
};

struct IssueQueueOut {
  wire<IQ_READY_NUM_WIDTH> free_slots = 0;
  IqStoredEntry issue_grants[ISSUE_WIDTH];
};

// ==========================================
// 2. IssueQueue (纯逻辑，不含IO)
// ==========================================
class IssueQueue {
public:
  IssueQueueIn in;
  IssueQueueOut out;

private:
  int size;
  int dispatch_width;
  std::vector<PortBinding> ports;
  int wake_words_per_row;

  std::vector<IqStoredEntry> entry;
  std::vector<IqStoredEntry> entry_1;
  int count, count_1;

  // Wakeup Matrix: [Physical Register] -> Bitmask of IQ slots
  std::vector<uint64_t> wake_matrix_src1;
  std::vector<uint64_t> wake_matrix_src2;
  std::vector<uint64_t> wake_matrix_src1_1;
  std::vector<uint64_t> wake_matrix_src2_1;


public:
  IssueQueue(const IssueQueueConfig &cfg)
      : size(cfg.size), dispatch_width(cfg.dispatch_width), // <--- 初始化
        ports(cfg.ports) {
    entry.resize(size);
    entry_1.resize(size);
    count = 0;
    count_1 = 0;
    wake_words_per_row = (size + 63) / 64;

    // Initialize segmented wakeup matrices:
    // [preg][word_idx], where each word tracks 64 IQ slots.
    wake_matrix_src1.resize(PRF_NUM * wake_words_per_row, 0);
    wake_matrix_src2.resize(PRF_NUM * wake_words_per_row, 0);
    wake_matrix_src1_1.resize(PRF_NUM * wake_words_per_row, 0);
    wake_matrix_src2_1.resize(PRF_NUM * wake_words_per_row, 0);

    in.enq_reqs.resize(dispatch_width);
  }

  void comb_begin() {
    entry_1 = entry;
    count_1 = count;
    wake_matrix_src1_1 = wake_matrix_src1;
    wake_matrix_src2_1 = wake_matrix_src2;
    out.free_slots = size - count;
    for (auto &grant : out.issue_grants) {
      grant = {};
    }
    for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
      in.wake_pregs[i] = 0;
      in.wake_valid[i] = 0;
    }
    in.issue_block = 0;
    in.flush_all = 0;
    in.flush_br = 0;
    in.flush_br_mask = 0;
    in.clear_mask = 0;
    for (auto &req : in.enq_reqs) {
      req = {};
    }
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      in.port_ready[i] = 0;
      in.port_fu_ready_mask[i] = 0;
    }
  }

  void comb_enq() {
    for (const auto &req : in.enq_reqs) {
      if (!req.valid) {
        continue;
      }
      int success = enqueue(req);
      Assert(success && "发射队列溢出！Dispatch 逻辑故障！");
    }
    out.free_slots = size - count_1;
  }

  void comb_wakeup() {
    for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
      if (!in.wake_valid[i]) {
        continue;
      }
      uint32_t preg = in.wake_pregs[i];
      if (preg >= PRF_NUM) {
        continue;
      }
      size_t row_base = matrix_row_base(preg);

      for (int w = 0; w < wake_words_per_row; w++) {
        uint64_t mask1 = wake_matrix_src1_1[row_base + w];
        while (mask1) {
          int bit = __builtin_ctzll(mask1);
          int idx = (w << 6) + bit;
          if (idx < size && entry_1[idx].valid && entry_1[idx].uop.src1_en &&
              entry_1[idx].uop.src1_busy && entry_1[idx].uop.src1_preg == preg) {
            entry_1[idx].uop.src1_busy = false;
          }
          mask1 &= (mask1 - 1);
        }
        wake_matrix_src1_1[row_base + w] = 0;

        uint64_t mask2 = wake_matrix_src2_1[row_base + w];
        while (mask2) {
          int bit = __builtin_ctzll(mask2);
          int idx = (w << 6) + bit;
          if (idx < size && entry_1[idx].valid && entry_1[idx].uop.src2_en &&
              entry_1[idx].uop.src2_busy && entry_1[idx].uop.src2_preg == preg) {
            entry_1[idx].uop.src2_busy = false;
          }
          mask2 &= (mask2 - 1);
        }
        wake_matrix_src2_1[row_base + w] = 0;
      }
    }
  }

  void comb_issue() {
    for (auto &grant : out.issue_grants) {
      grant = {};
    }
    std::vector<std::pair<int, int>> scheduled_pairs = schedule();
    std::vector<int> committed_indices;
    committed_indices.reserve(scheduled_pairs.size());

    for (const auto &pair : scheduled_pairs) {
      int entry_idx = pair.first;
      int phys_port = pair.second;
      uint64_t req_bit = (1ULL << static_cast<uint32_t>(entry[entry_idx].uop.op));
      if (in.port_ready[phys_port] && (in.port_fu_ready_mask[phys_port] & req_bit) &&
          !in.issue_block) {
        out.issue_grants[phys_port].valid = true;
        out.issue_grants[phys_port].uop = entry[entry_idx].uop;
        committed_indices.push_back(entry_idx);
      }
    }

    commit_issue(committed_indices);
    out.free_slots = size - count_1;
  }

  void comb_flush() {
    if (in.flush_all) {
      flush_all();
    } else if (in.flush_br) {
      flush_br(in.flush_br_mask);
    }
    if (in.clear_mask) {
      clear_br(in.clear_mask);
    }
    out.free_slots = size - count_1;
  }

private:
  // 入队 (返回成功入队的个数)
  int enqueue(const IqStoredEntry &inst) {
    if (count_1 >= size)
      return 0;
    for (int i = 0; i < size; i++) {
      if (!entry_1[i].valid) {
        entry_1[i] = inst;
        entry_1[i].valid = true;
        set_dep_bits_for_slot(entry_1[i], i);

        count_1++;
        return 1;
      }
    }
    return 0;
  }

  // Flush
  void flush_br(wire<BR_MASK_WIDTH> br_mask) {
    for (int i = 0; i < size; i++) {
      if (!entry_1[i].valid) {
        continue;
      }
      bool match_mask = (entry_1[i].uop.br_mask & br_mask) != 0;
      if (match_mask) {
        clear_dep_bits_for_slot(entry_1[i], i);
        entry_1[i].valid = false;
        count_1--;
      }
    }
  }

  // Clear resolved branch bits from surviving entries
  void clear_br(wire<BR_MASK_WIDTH> clear_mask) {
    if (clear_mask == 0) return;
    for (int i = 0; i < size; i++) {
      if (entry_1[i].valid) {
        entry_1[i].uop.br_mask &= ~clear_mask;
      }
    }
  }

  void flush_all() {
    for (auto &e : entry_1)
      e.valid = false;
    count_1 = 0;
    
    // Clear Wakeup Matrices
    std::fill(wake_matrix_src1_1.begin(), wake_matrix_src1_1.end(), 0);
    std::fill(wake_matrix_src2_1.begin(), wake_matrix_src2_1.end(), 0);
  }

  // 提交调度结果 (将选中的指令移出队列)
  void commit_issue(const std::vector<int> &indices) {
    for (int idx : indices) {
      if (entry_1[idx].valid) {
        clear_dep_bits_for_slot(entry_1[idx], idx);
        entry_1[idx].valid = false;
        count_1--;
      }
    }
  }

public:
  void seq() {
    entry = entry_1;
    count = count_1;
    wake_matrix_src1 = wake_matrix_src1_1;
    wake_matrix_src2 = wake_matrix_src2_1;
  }

private:
  std::vector<std::pair<int, int>> schedule() {
    std::vector<std::pair<int, int>> result;

    // 1. 端口忙闲状态标记
    int num_ports = ports.size();
    bool port_busy[ISSUE_WIDTH] = {false};
    assert(num_ports <= ISSUE_WIDTH &&
           "IssueQueue ports exceed ISSUE_WIDTH capacity");

    auto try_issue_entry = [&](int entry_idx, int &issued_count) {
      if (!entry[entry_idx].valid || !is_ready(entry[entry_idx])) {
        return;
      }

      uint32_t op_type = entry[entry_idx].uop.op;
      uint64_t op_bit = (1ULL << op_type); // 当前指令的特征位

      // 3. 寻找匹配的端口 (First-Fit 策略)
      int selected_port_idx = -1; // 这是 ports 数组的下标，不是物理端口号
      for (int p = 0; p < num_ports; p++) {
        if (!port_busy[p] && (ports[p].capability_mask & op_bit)) {
          selected_port_idx = p;
          break;
        }
      }

      // 4. 发射成功
      if (selected_port_idx != -1) {
        // 记录结果：<IQ中的Index, 物理Port号>
        result.push_back({entry_idx, ports[selected_port_idx].port_idx});
        // 标记资源被占用
        port_busy[selected_port_idx] = true;
        issued_count++;
      }
    };

    int issued_count = 0;
    if (ISSUE_SCHEDULE_POLICY == IssueSchedulePolicy::IQ_SLOT_PRIORITY) {
      // 固定编号优先：按 IQ 槽位编号从小到大扫描。
      int seen_valid = 0;
      for (int i = 0; i < size && issued_count < num_ports; i++) {
        if (entry[i].valid) {
          seen_valid++;
        }
        try_issue_entry(i, issued_count);
        if (seen_valid >= count) {
          break;
        }
      }
    } else {
      // ROB oldest-first：按 (rob_flag, rob_idx) 年龄排序后再分配端口。
      std::vector<int> ready_indices;
      ready_indices.reserve(size);
      int seen_valid = 0;
      for (int i = 0; i < size; i++) {
        if (!entry[i].valid) {
          continue;
        }
        seen_valid++;
        if (is_ready(entry[i])) {
          ready_indices.push_back(i);
        }
        if (seen_valid >= count) {
          break;
        }
      }

      auto older_than = [&](int lhs, int rhs) {
        const auto &a = entry[lhs].uop;
        const auto &b = entry[rhs].uop;
        if (a.rob_flag == b.rob_flag) {
          if (a.rob_idx != b.rob_idx) {
            return a.rob_idx < b.rob_idx;
          }
        } else {
          if (a.rob_idx != b.rob_idx) {
            return a.rob_idx > b.rob_idx;
          }
        }
        return lhs < rhs; // Stable tie-breaker.
      };
      std::sort(ready_indices.begin(), ready_indices.end(), older_than);

      for (int idx : ready_indices) {
        if (issued_count >= num_ports) {
          break;
        }
        try_issue_entry(idx, issued_count);
      }
    }

    return result;
  }
  size_t matrix_row_base(uint32_t preg) const {
    return static_cast<size_t>(preg) * static_cast<size_t>(wake_words_per_row);
  }

  uint64_t slot_bit(int idx) const {
    return 1ULL << (idx & 63);
  }

  int slot_word(int idx) const {
    return idx >> 6;
  }

  void set_dep_bits_for_slot(const IqStoredEntry &ent, int idx) {
    int word = slot_word(idx);
    uint64_t bit = slot_bit(idx);

    if (ent.uop.src1_en && ent.uop.src1_busy) {
      uint32_t preg = ent.uop.src1_preg;
      if (preg < PRF_NUM) {
        wake_matrix_src1_1[matrix_row_base(preg) + word] |= bit;
      }
    }
    if (ent.uop.src2_en && ent.uop.src2_busy) {
      uint32_t preg = ent.uop.src2_preg;
      if (preg < PRF_NUM) {
        wake_matrix_src2_1[matrix_row_base(preg) + word] |= bit;
      }
    }
  }

  void clear_dep_bits_for_slot(const IqStoredEntry &ent, int idx) {
    int word = slot_word(idx);
    uint64_t bit = slot_bit(idx);
    uint64_t clear_mask = ~bit;

    if (ent.uop.src1_en) {
      uint32_t preg = ent.uop.src1_preg;
      if (preg < PRF_NUM) {
        wake_matrix_src1_1[matrix_row_base(preg) + word] &= clear_mask;
      }
    }
    if (ent.uop.src2_en) {
      uint32_t preg = ent.uop.src2_preg;
      if (preg < PRF_NUM) {
        wake_matrix_src2_1[matrix_row_base(preg) + word] &= clear_mask;
      }
    }
  }

  bool is_ready(const IqStoredEntry &ent) {
    const auto &op = ent.uop;
    bool ops_ok =
        (!op.src1_en || !op.src1_busy) && (!op.src2_en || !op.src2_busy);
    return ops_ok ;
  }
};
