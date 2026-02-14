# Rob (Reorder Buffer) 设计文档

## 1. 概述 (Overview)
ROB (Reorder Buffer) 是乱序执行处理器维护程序顺序 (Program Order) 的核心组件。所有的指令在 Dispatch 阶段按序分配 ROB 条目，在 Execute 阶段乱序完成，最终在 Commit 阶段按序提交。ROB 负责确保处理器的精确异常 (Precise Exception) 和分支误预测恢复。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dis2rob.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输入 | Dispatch | 新分派指令的 ROB 请求 |
| `exu2rob.entry` | `ISSUE_WIDTH * sizeof(Entry)` | 输入 | Exu | **早期完成通知 (Complete)**：指令执行结束信号 |
| `dec_bcast.mispred` | 1 | 输入 | Idu | 分支误预测信号 |
| `csr2rob.interrupt` | 1 | 输入 | Csr | 外部中断请求 |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `rob2dis.enq_idx` | `log2(ROB_LINES)` | 输出 | Dispatch | 当前分配指针 (行索引) |
| `rob2dis.stall` | 1 | 输出 | Dispatch | 如果 ROB 满或遇到序列化指令，反压前端 |
| `rob_commit` | `COMMIT_WIDTH * sizeof(Entry)`| 输出 | Arch | 指令提交总线 (写回 Arch RAT / Store Queue) |
| `rob_bcast.flush` | 1 | 输出 | All | 全局冲刷信号 (异常、中断、特殊指令) |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 多体存储结构 (Multi-Banked Organization)
为了支持每周期提交多条指令 (`COMMIT_WIDTH`)，ROB 采用了**多体交叉 (Interleaved Banking)** 设计：
- **行 (Line)**: 这里的“指针” `enq_ptr/deq_ptr` 实际上是指向“行”的。
- **体 (Bank)**: 每一行包含 `ROB_BANK_NUM` 个条目 (即 `COMMIT_WIDTH`)。
- **寻址**: 逻辑 ROB ID = `Line_Index * BANK_NUM + Bank_Index`。指令按顺序填充到同一行的不同 Bank 中。

### 3.2 提交策略 (Commit Strategy)
ROB 实现了两种提交模式：
1.  **组提交 (Group Commit)**: 默认模式。
    *   检查 `deq_ptr` 指向的整行指令。
    *   只有当该行中**所有有效指令都已完成 (`cplt_num == uop_num`)** 时，才允许该行整体退休。
    *   以此实现高带宽提交。
2.  **单条提交 (Single Commit)**: 特殊模式。
    *   当队头遇到特殊指令（如 CSR、EBREAK、WFI）或发生中断/异常时触发。
    *   强制一次只提交一条指令，确保非推测执行和精确状态更新。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_ready` (分配检查与反压)
- **功能描述**：检查 ROB 是否满 (`enq_ptr == deq_ptr` 且 `flag` 不同)。
- **特殊处理**：如果队头指令是 Flush 类型指令而未提交，也会产生 `stall`，防止推测执行越过同步点。
- **性能分析**：在此阶段判断队头停顿原因 (Memory Bound / Core Bound)，驱动 TMA 计数器。

### 4.2 `comb_fire` (入队)
- **功能描述**：根据 Dispatch 的 `dis_fire` 信号，将新指令写入 `enq_ptr` 指向的行，并推进指针。

### 4.3 `comb_complete` (完成标记)
- **功能描述**：监听 `exu2rob` (Backend Exec) 和 `lsu2rob` (Memory Interface) 完成总线。
- **逻辑**：根据回传指令的 `rob_idx` 找到对应条目，增加其 `cplt_num`。由于优化后的后端过滤了部分 PRF 写回（如分支/存储），此总线是 ROB 获知指令执行状态的唯一数据源。当 `cplt_num == uop_num` 时，该条目视为已完成。
- **异常记录**：如果遭遇 Page Fault 或分支误预测，在此阶段将异常信息记录到 ROB 条目中，**但不立即触发**，而是等到提交阶段。

### 4.4 `comb_commit` (提交与异常触发)
- **功能描述**：决定是否推进 `deq_ptr`。
- **异常处理**：
  - 这是处理异常的唯一时刻（精确异常）。
  - 如果队头指令标记了 Exception/Flush，则置起全局 `rob_bcast.flush` 信号。
  - 根据异常类型（Syscall, Page Fault, Interrupt），设置 CSR 状态或重定向 PC。

### 4.5 `comb_branch` (误预测恢复)
- **功能描述**：处理分支误预测的回滚。
- **逻辑**：将 `enq_ptr` (Tail) 重置为误预测分支所在的下一行。同时，非法化 (Invalidate) 从重定向点到旧 Tail 之间的所有中间条目，清除推测路径上的指令。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `slots_core_bound_rob` | ROB 满 | Core Bound | 由于 ROB 满导致 Dispatch 阻塞 |
| `slots_retiring` | 提交指令数 | Retiring | 成功提交的指令槽位数 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `entry` | `LINE * BANK * Entry` | `reg` array | ROB 存储体 |
| `enq/deq_ptr` | `log2(LINE)` | `reg` |读写指针 (Cursor) |
| `enq/deq_flag` | 1 | `reg` | 阶段标志位 (Phase Bit)。用于区分满/空状态及辅助年龄比较。 |

> [!NOTE] 满空判断与年龄比较
> - **满空判断**: 当 `ptr` 相等时，如果 `flag` 相等则为空，不等则为满。
> - **年龄比较**: ROB ID 实际上由 `{flag, ptr, bank_idx}` 组成。通过比较 Flag 位可以判断指令是否跨越了 Buffer 的一圈，从而正确计算新旧关系。

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Complete Ports | `ISSUE_WIDTH` | 接收乱序完成信号的写入端口 |
| Commit Ports | `COMMIT_WIDTH` | 读出提交指令的读取端口 |
| Exception Logic | 1 | 集中式异常仲裁与冲刷逻辑 |
