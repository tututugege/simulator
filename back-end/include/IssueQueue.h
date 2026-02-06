#include "config.h"
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

// ==========================================
// 2. IssueQueue (纯逻辑，不含IO)
// ==========================================
class IssueQueue {
public:
  int id;
  int size;
  int dispatch_width;
  std::vector<PortBinding> ports;

  std::vector<InstEntry> entry;
  std::vector<InstEntry> entry_1;
  int count, count_1;

  IssueQueue(const IssueQueueConfig &cfg)
      : id(cfg.id), size(cfg.size),
        dispatch_width(cfg.dispatch_width), // <--- 初始化
        ports(cfg.ports) {

    entry.resize(size);
    entry_1.resize(size);
    count = 0;
    count_1 = 0;
  }

  // 入队 (返回成功入队的个数)
  int enqueue(const InstEntry &inst) {
    if (count_1 >= size)
      return 0;
    for (int i = 0; i < size; i++) {
      if (!entry_1[i].valid) {
        entry_1[i] = inst;
        entry_1[i].valid = true;
        count_1++;
        return 1;
      }
    }
    return 0;
  }

  // 唤醒逻辑
  void wakeup(bool *valid_flags, uint32_t *pregs, int num_wakeups) {
    for (int i = 0; i < size; i++) {
      if (entry_1[i].valid) {
        InstUop &uop = entry_1[i].uop;
        for (int j = 0; j < num_wakeups; j++) {
          if (valid_flags[j]) {
            if (uop.src1_en && uop.src1_preg == pregs[j])
              uop.src1_busy = false;
            if (uop.src2_en && uop.src2_preg == pregs[j])
              uop.src2_busy = false;
          }
        }
      }
    }
  }

  // 特殊唤醒：Load 依赖清除
  void clear_store_mask(int stq_idx, bool is_addr) {
    for (int i = 0; i < size; i++) {
      if (entry_1[i].valid) {
        uint32_t mask = (1 << stq_idx);
        if (is_addr)
          entry_1[i].uop.pre_sta_mask &= ~mask;
      }
    }
  }

  // Flush
  void flush_br(uint32_t br_mask) {
    for (int i = 0; i < size; i++) {
      if (entry_1[i].valid && ((1 << entry_1[i].uop.tag) & br_mask)) {
        entry_1[i].valid = false;
        count_1--;
      }
    }
  }

  void flush_all() {
    for (auto &e : entry_1)
      e.valid = false;
    count_1 = 0;
  }

  // 提交调度结果 (将选中的指令移出队列)
  void commit_issue(const std::vector<int> &indices) {
    for (int idx : indices) {
      if (entry_1[idx].valid) {
        entry_1[idx].valid = false;
        count_1--;
      }
    }
  }

  void tick() {
    entry = entry_1;
    count = count_1;
  }

  std::vector<std::pair<int, int>> schedule() {
    std::vector<std::pair<int, int>> result;

    // 1. 端口忙闲状态标记
    int num_ports = ports.size();
    std::vector<bool> port_busy(num_ports, false);

    // 2. 遍历队列 (Oldest-First Scan)
    int issued_count = 0;

    for (int i = 0; i < size; i++) {
      // 如果端口都满了，提前退出 (优化)
      if (issued_count >= num_ports)
        break;

      if (entry[i].valid && is_ready(entry[i])) {
        UopType op_type = entry[i].uop.op;
        uint64_t op_bit = (1ULL << op_type); // 当前指令的特征位

        // 3. 寻找匹配的端口 (First-Fit 策略)
        int selected_port_idx = -1; // 这是 ports 数组的下标，不是物理端口号

        for (int p = 0; p < num_ports; p++) {
          if (!port_busy[p]) {
            // 检查能力匹配
            if (ports[p].capability_mask & op_bit) {
              selected_port_idx = p;
              break;
            }
          }
        }

        // 4. 发射成功
        if (selected_port_idx != -1) {
          // 记录结果：<IQ中的Index, 物理Port号>
          result.push_back({i, ports[selected_port_idx].port_idx});

          // 标记资源被占用
          port_busy[selected_port_idx] = true;
          issued_count++;
        }
      }
    }

    return result;
  }

  // 用于 Store Mask 扫描的只读访问
  const std::vector<InstEntry> &get_entries_1() const { return entry_1; }

private:
  bool is_ready(const InstEntry &ent) {
    const auto &op = ent.uop;
    bool ops_ok =
        (!op.src1_en || !op.src1_busy) && (!op.src2_en || !op.src2_busy);
    bool mem_ok = true;
    if (op.op == UOP_LOAD) { // 假设 op_type
      mem_ok = (op.pre_sta_mask == 0);
    }
    return ops_ok && mem_ok;
  }
};
