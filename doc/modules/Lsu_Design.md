# Lsu (Load Store Unit) 设计文档

## 1. 概述 (Overview)
LSU (Load Store Unit) 负责处理所有的访存请求，维护内存一致性模型 (TSO/Weak) 并管理处理器与缓存层级之间的交互。
本项目目前的实现为 **`SimpleLsu`**，它提供了一个功能完整的 LSU 模型，但采用了理想化的地址转换接口 (`va2pa`)，并不包含真实的 TLB 硬件逻辑。为了支持未来的扩展（如加入 TLB 和 Page Walker），代码预留了 **`AbstractLsu`** 抽象基类接口。
核心组件包括 Store Buffer (SB/STQ)、Load Queue (LQ) 以及用于地址转换的 MMU 和简易 Cache 模型。

LSU 的关键特性包括：
- **Store Buffer**: 支持投机性 Store 执行，直到提交阶段才写入内存。
- **Store-to-Load Forwarding (STLF)**: 允许 Load 指令从尚未提交的 Store Buffer 中直接读取数据。
- **Memory Disambiguation**: 动态检查访存依赖，处理地址别名问题。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dis2lsu.alloc_req` | `MAX_STQ_WIDTH * 1` | 输入 | Dispatch | 申请 STQ 条目请求 |
| `exe2lsu.agu_req` | `AGU_COUNT * sizeof(Uop)` | 输入 | Exu/AGU | 访存地址 (Load/Store Addr) |
| `exe2lsu.sdu_req` | `SDU_COUNT * sizeof(Uop)` | 输入 | Exu/SDU | Store 数据 (Store Data) |
| `rob_commit` | `COMMIT_WIDTH` | 输入 | ROB | Store 提交通知 |
| `rob_bcast.flush` | 1 | 输入 | ROB | 全局冲刷信号 |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `lsu2dis.stq_free/ldq_free` | `log2(STQ_SIZE)/log2(LDQ_SIZE)` | 输出 | Dispatch | STQ/LDQ 剩余空间，用于反压 |
| `lsu2exe.wb_req` | `WB_WIDTH * sizeof(Uop)` | 输出 | Prf/Exu | Load 写回请求 (含数据) |
| `lsu2rob.miss_mask`| `ROB_NUM` | 输出 | ROB | 标记哪些 ROB Index 对应的 Load 处于 Cache Miss 状态 (用于 TMA Memory Bound 分析) |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 Store Queue (STQ)
STQ 是一个环形缓冲区 (Circular Buffer)，用于管理 Store 指令的生命周期：
*   **指针管理**：
    *   `stq_tail`: 分配指针 (Dispatch Stage)。
    *   `stq_commit`: 提交指针 (Commit Stage)，划分投机态与非投机态。
    *   `stq_head`: 退休指针 (Retire Stage)，只有当 Head 指向的指令已被 ROB 确认提交，且数据地址均就绪时，才真正写入 Memory/Cache。
*   **Split Transaction**: Store 操作被拆分为 STA (地址) 和 STD (数据) 两个微操作，它们在 Execution 阶段乱序到达 LSU，但在 STQ 中合并。

### 3.2 In-flight Load Queue (LQ)
LSU 维护一个 `std::list<MicroOp> inflight_loads` 来追踪已被发射但尚未完成写回的 Load 指令。
*   此队列模拟 Cache 访问延迟。
*   负责执行 Store-to-Load Forwarding 检查。

### 3.3 Store-to-Load Forwarding (STLF)
当 Load 指令执行时，LSU 会扫描 STQ（从 Head 到 Tail）：
1.  **Match**: 找到地址匹配且 Age 更老 (Older) 的 Store。
2.  **Forward**: 如果该 Store 数据已就绪，直接将数据转发给 Load (0 Cycle Latency from Memory Perspective)。
3.  **Stall (Retry)**: 如果地址匹配但数据未就绪（STD 还没来），该 Load 必须等待，直到数据到达。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_stq_alloc` (资源预留)
- **功能描述**：根据 Dispatch 阶段的请求，推进 `stq_tail` 指针。
- **逻辑**：简单的模运算加法。需注意 Dispatch 阶段已保证 `stq_free > 0`。

### 4.2 `comb_recv` (执行请求处理)
- **功能描述**：接收 AGU 和 SDU 的请求。
- **Store Addr (STA)**: 写入 STQ 对应条目的 `address` 字段，并进行 MMU 地址转换。若发生 Page Fault，立即标记异常。
- **Store Data (STD)**: 写入 STQ 对应条目的 `data` 字段。
- **Load Handling**: 调用 `handle_load_req`，执行 STLF 检查或 Cache 访问，计算完成时间 (`cplt_time`)。

### 4.3 `comb_commit` (提交确认)
- **功能描述**：监听 ROB 的提交总线。
- **逻辑**：当 ROB 提交一条 Store 指令时，LSU 推进 `stq_commit` 指针，将该 Store 标记为“非投机” (Non-speculative)。

### 4.4 `comb_load_res` (写回)
- **功能描述**：从 `finished_loads` 队列中取出已完成（Cache Hit 或 Forward 成功）的 Load 指令，发送给 Writeback 端口。

### 4.5 `comb_flush` (冲刷与恢复)
- **Global Flush**: 清空 LQ，重置 `stq_tail = stq_commit` (丢弃所有投机 Store)。
- **Mispred**:
  - `find_recovery_tail`: 扫描 STQ，找到第一个依赖误预测分支的 Store，将 `stq_tail` 回滚到该位置。
  - 清理 LQ 中被 Squash 的 Load 指令。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `slots_mem_l1_bound` | L1 Cache 阻塞 | Memory Bound | 由于 L1 资源(MSHR)或 LSU 内部阻塞导致的停顿 |
| `slots_mem_ext_bound`| 外部内存阻塞 | Memory Bound | 由于 LLC Miss 访问主存导致的流水线停顿 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `stq` | `STQ_NUM * sizeof(Entry)` | `reg` array | Store Queue 存储体 |
| `inflight_loads` | Dynamic | `list` | Load Queue (非硬件实现，模拟链表) |
| `finished_loads` | Dynamic | `fifo` | 写回缓冲 |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| STLF Logic | `AGU_COUNT` comparators | 用于 Load 地址与 STQ 中所有地址的并行比较逻辑 |
| MMU Ports | - | 理想化地址转换接口 (`va2pa`)，无 TLB 硬件开销 |
