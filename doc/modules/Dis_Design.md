# Dis (Dispatch) 设计文档

## 1. 概述 (Overview)
Dispatch 模块位于后端流水线的**顺序重命名**和**乱序发射**之间。其核心职责是将已经重命名的指令分解（Decomposition）为具体的微操作（UOps），并将其分派至对应的发射队列 (Issue Queue)、重排序缓存 (ROB) 以及访存队列 (LSU)。该模块是**后端带宽的瓶颈点**，必须在一个周期内完成所有下游资源的可用性检查。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `ren2dis.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输入 | Rename | 已重命名的指令流 |
| `ren2dis.valid` | `FETCH_WIDTH * 1` | 输入 | Rename | 输入有效位 |
| `rob2dis.ready` | 1 | 输入 | ROB | ROB 是否有空间接收新指令组 |
| `iss2dis.ready_num`| `IQ_NUM * log2(IQ_SIZE)` | 输入 | Isu | 各个发射队列当前的空闲条目数 |
| `lsu2dis.ldq/stq_free` | `log2(LDQ_SIZE)/log2(STQ_SIZE)` | 输入 | Lsu | Load/Store 队列的空闲空间 |
| `iss_awake/prf_awake` | - | 输入 | Isu/Prf | 唤醒总线，用于消除分派阶段的 RAW 依赖 |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dis2ren.ready` | 1 | 输出 | Rename | 反压信号，指示 Dispatch 是否接受了指令 |
| `dis2rob.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输出 | ROB | 分派至 ROB 的指令信息（含拆分后的 uops 数量） |
| `dis2iss.req` | `IQ_NUM * WIDTH * sizeof(Uop)` | 输出 | Isu | 分派至各个 Issue Queue 的微操作请求 |
| `dis2lsu.alloc_req`| `MAX_STQ_WIDTH * 1` | 输出 | Lsu | 向 Store Queue 申请条目的请求 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 指令拆分逻辑 (Instruction Decomposition)
Dispatch 负责将架构指令（Macro-Op）拆解为后端执行所需的微指令（Micro-Op）。
- **1-to-1 映射**：大多数 ALU 指令（ADD, MUL, DIV）直接映射为一个 Uop。
- **1-to-2 映射**：
  - `STORE`: 拆分为 `STA` (Store Address) 和 `STD` (Store Data)。
  - `JAL/JALR`: 拆分为 `ADD` (计算 PC+4 写回) 和 `JUMP` (执行跳转)。
- **1-to-N 映射**：
  - `AMO` (Atomic Memory Operation): 拆分为 `LOAD` + `STA` + `STD` (针对 SC/RMW 序列)。

### 3.2 统一资源检查 (Unified Resource Check)
采用**全有或全无 (All-or-Nothing)** 的分派策略。对于一个 Fetch Packet 中的每一条指令，必须同时满足以下所有条件才能 Dispatch 成功，否则该指令及其后续所有指令都会 **Stall**：
1.  **ROB 空间**：ROB 必须有空闲行。
2.  **IQ 空间**：目标发射队列（如 IQ_INT, IQ_LD, IQ_STA）必须有足够的空闲条目。
3.  **LSU 空间**：若是访存指令，Load Queue 或 Store Queue 必须有空闲条目。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_alloc` (资源预分配)
- **功能描述**：为指令流分配 ROB ID 和 LSU 队列条目。
- **LSU 分配**：按序扫描 Packet 中的 Load/Store 指令，检查 LDQ/STQ 空间。
  - **Store**: 分配 STQ 条目，推进 `stq_tail`。
  - **Load**: 记录当前的 `stq_tail` 快照 (`stq_idx`)，用于后续 LSU 阶段的依赖检查。不再生成 `pre_sta_mask`。
- **ROB ID**：为所有有效指令生成对应的 ROB 索引。

### 4.2 `comb_wake` (分派阶段唤醒)
- **功能描述**：监听 `prf_awake` (执行完成) 和 `iss_awake` (发射唤醒) 总线。
- **目的**：如果在 Dispatch 阶段，指令的源操作数刚刚被唤醒（例如前一条指令刚执行完，或者刚被发射），Dispatch 模块会立即清除该源操作数的 `busy` 位。这通过旁路 (Bypass) 实现了 **0 周期分派-发射延迟**。

### 4.3 `comb_dispatch` (拆分与发射检查)
- **核心逻辑**：
  1.  调用 `decompose_inst` 将指令拆分为 Uops。
  2.  检查每个 Uop 对应的 IQ 是否有空闲空间 (`ready_num`) 和写入端口限制 (`dispatch_width`)。
  3.  若检查通过，将 Uop 写入 `dis2iss` 对应端口，并在 `dispatch_cache` 中记录元数据供后续回滚使用。

### 4.4 `comb_fire` (最终确认与回滚)
- **功能描述**：综合上游有效性、ROB 状态和 `comb_dispatch` 的检查结果，生成最终的 `fire` 信号。
- **回滚机制**：由于 `comb_dispatch` 是推测性地写入了 `dis2iss`，如果后续检查（如 ROB 满或 CSR 序列化要求）导致 Fire 失败，必须根据 `dispatch_cache` 中的记录，**撤销**已经发出的 Issue 请求，防止“幽灵指令”进入发射队列。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `slots_frontend_bound` | 前端缺供 | Frontend Bound | Dispatch 阶段没有有效指令输入 |
| `slots_backend_bound` | 后端阻塞 | Backend Bound | 有指令但由于资源不足无法 Dispatch |
| `slots_core_bound_rob` | ROB 满 | Core Bound | 由于 ROB 空间不足导致的阻塞 |
| `slots_core_bound_iq` | IQ 满 | Core Bound | 由于发射队列空间不足导致的阻塞 |
| `slots_mem_bound_lsu` | LSU 满 | Memory Bound | 由于 LDQ/STQ 空间不足导致的阻塞 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `inst_r` | `FETCH_WIDTH * sizeof(InstEntry)` | `reg` | 流水线寄存器 |
| `dispatch_cache` | `FETCH_WIDTH * MetaData` | `reg` | 用于在组合逻辑阶段间传递拆分信息的临时存储 |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Instruction Decomposer | `FETCH_WIDTH` units | 并行指令拆分逻辑 (支持 1-to-3 拆分) |
| Issue Port Crossbar | `FETCH_WIDTH` x `IQ_NUM` | 将拆分后的 Uops 路由到不同 IQ 的交叉开关 |
| Wakeup Comparators | `FETCH_WIDTH * 2 * WakePorts` | 用于 Dispatch 阶段唤醒的源操作数比较器 |
