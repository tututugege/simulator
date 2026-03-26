# Isu (Issue Unit) 设计文档

## 1. 概述 (Overview)
ISU (Issue Unit) 是乱序执行引擎的调度核心，负责管理等待执行的指令。其主要功能包括：
1.  **指令缓存**：接收分派 (Dispatch) 的指令并将其存储在对应的发射队列 (Issue Queue) 中。
2.  **唤醒 (Wakeup)**：监听结果广播总线，更新等待指令的操作数就绪状态。
3.  **选择 (Select)**：根据功能单元 (FU) 的可用性，从就绪指令中选择最优者发射。
4.  **发射 (Issue)**：读取物理寄存器堆 (PRF) 并将指令发送至执行单元 (Exu)。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dis2iss.req` | `IQ_NUM * WIDTH * sizeof(Uop)` | 输入 | Dispatch | 分派来的微指令请求 |
| `prf_awake` | `LSU_WIDTH * sizeof(Wake)` | 输入 | Prf/Lsu | 来自写回/Load 的慢速唤醒信号 |
| `exe2iss.ready`| `ISSUE_WIDTH * 1` | 输入 | Exu | 执行单元各端口的 Busy 状态 |
| `rob_bcast.flush` | 1 | 输入 | ROB | 流水线冲刷信号 |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `iss2dis.ready_num` | `IQ_NUM * log2(IQ_SIZE)` | 输出 | Dispatch | 各发射队列当前的剩余容量 |
| `iss2prf.iss_entry` | `ISSUE_WIDTH * sizeof(Entry)` | 输出 | Prf/Exu | 发射出的指令（包含操作数物理地址） |
| `iss_awake` | `MAX_WAKE_PORTS * sizeof(Wake)` | 输出 | Ren/Dis | Isu 产生的所有唤醒信号汇总（含快速唤醒） |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 分布式发射队列 (Distributed Issue Queues)
本项目采用**分布式发射队列**设计，针对不同类型的指令设立独立的队列结构：
- **IQ_INT**: 整数运算指令 (含 CSR/MUL/DIV)
- **IQ_LD**: Load 指令
- **IQ_STA**: Store Address 指令
- **IQ_STD**: Store Data 指令
- **IQ_BR**: 分支指令

### 3.2 矩阵式唤醒 (Wakeup Matrix)
传统的发射队列依赖 CAM (Content Addressable Memory) 逻辑，即每个指令槽位监听结果总线并比较 Tag，这会带来 $O(N)$ 的复杂度和高功耗。本模块引入了 **Wakeup Matrix** 机制，将唤醒复杂度降低至 $O(1)$。

#### 3.2.1 核心原理
Wakeup Matrix 维护一个全局位矩阵，记录“哪些指令正在等待某个物理寄存器”：
- **行索引**：物理寄存器号 (Physical Register Index)。
- **内容**：位掩码 (Bitmask)，第 $i$ 位为 1 表示 IQ 中的第 $i$ 号槽位正在等待该寄存器。

#### 3.2.2 工作流程
1.  **入队 (Dispatch)**: 当指令进入 IQ 分配到槽位 `idx` 时，若源操作数 `src` 未就绪，则将矩阵中 `row[src]` 的第 `idx` 位置 1。
2.  **唤醒 (Wakeup)**: 当物理寄存器 `preg` 写回时，直接读取 `row[preg]` 获得所有等待该寄存器的指令掩码，一次性将它们标记为就绪，并清除该行。
3.  **优势**: 避免了全局广播和比较，降低了功耗并优化了时序路径。

### 3.3 延迟流管线 (Latency Pipeline)
为了支持多周期指令（如 MUL/DIV）的背靠背唤醒，ISU 内部维护了一个**延迟唤醒移位寄存器**：
- 发射多周期指令时，将其目的寄存器推入 Pipe。
- Pipe每周期移位，当计数器归零时触发 `iss_awake`，确保依赖指令恰好在结果产生时被唤醒。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_ready` (容量通告)
- **功能描述**：计算每类 IQ 的剩余空间 (`size - count`)，反馈给 Dispatch 模块以进行流控。

### 4.2 `comb_enq` (指令入队)
- **功能描述**：将 Dispatch 发来的指令写入对应的 IQ 空闲槽位。
- **关键逻辑**：
  - 更新 Wakeup Matrix，标记源操作数依赖。
  - 对于 Load 指令，不再生成初始的 `store_mask`（依赖检查已移至 LSU 阶段）。

### 4.3 `comb_issue` (选择与发射)
- **功能描述**：遍历所有 IQ，从就绪指令（操作数 Ready & Memory Ready）中选择最老的一条（Oldest-First）。
- **资源仲裁**：检查目标发射端口（Exu Port）是否空闲 (`exe2iss.ready`)。若成功，将指令读出并清除 IQ 条目。

### 4.4 `comb_awake` (唤醒汇总)
- **功能描述**：汇总来自三方面的唤醒事件：
  1.  **慢速唤醒**：来自 `prf_awake` (Load/Cache Miss)。
  2.  **延迟唤醒**：来自 `latency_pipe` (多周期运算完成)。
  3.  **快速唤醒**：来自本周期刚刚发射的单周期指令 (ALU)。
- **操作**：驱动唤醒矩阵，更新所有 IQ 中指令的 `src_busy` 状态。不再包含 `store_mask` 清除逻辑。

### 4.5 `comb_calc_latency_next` (下一周期延迟更新)
- **功能描述**：更新延迟流水线状态，处理新发射的多周期指令。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `slots_core_bound_iq` | 发射队列满 | Core Bound | 由于 IQ 满导致 Dispatch 阻塞的次数 |
| `issue_rate` | 发射率 | Execution | 每周期平均发射的微指令数 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `IQ Entries` | `Sum(IQ_SIZE) * sizeof(Uop)` | `reg` array | 分布式发射队列及其 Payload |
| `Wake MatrixSrc1/2`| `PRF_NUM * IQ_TOTAL_SIZE` | `reg`/SRAM | 唤醒依赖矩阵 |
| `Latency Pipe` | `MAX_LATENCY * 32` | `reg` shift | 多周期唤醒倒计时链表 |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Wakeup Ports | `MAX_WAKEUP_PORTS` | 支持同时唤醒的寄存器数量（决定矩阵行读取带宽） |
| Issue Ports | `ISSUE_WIDTH` | 最大并发发射宽度 |
| Selection Logic | `IQ_NUM` Arbiters | 每个队列独立的 Oldest-First 选择器 |
