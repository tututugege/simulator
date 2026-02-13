# Csr (Control and Status Register) 设计文档

## 1. 概述 (Overview)
CSR (Control and Status Register) 模块负责管理处理器的特权级状态、中断与异常处理以及系统控制寄存器。它实现了 RISC-V 特权级规范 (Privileged Spec) 的核心逻辑，包括 M-Mode 和 S-Mode 的支持。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `exe2csr` | - | 输入 | Exu | CSR 指令的读写请求 (Execute 阶段) |
| `rob2csr.commit` | 1 | 输入 | ROB | CSR 指令提交信号 (Commit 阶段) |
| `rob_bcast` | - | 输入 | ROB | 全局异常/中断/返回信号 (含 Trap Cause, PC 等) |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `csr2exe.rdata` | 32 | 输出 | Exu | CSR 读数据 (组合逻辑直连读取，0周期延迟) |
| `csr2rob.interrupt_req` | 1 | 输出 | ROB | 中断挂起请求 (通知 ROB 尝试处理) |
| `csr2front.trap_pc` | 32 | 输出 | Fetch | 异常/中断发生时的跳转目标地址 (MTVEC/STVEC) |
| `csr2front.epc` | 32 | 输出 | Fetch | `MRET`/`SRET` 返回地址 (MEPC/SEPC) |
| `csr_status` | - | 输出 | MMU/Lsu | 提供 SATP, Privilege Mode, STATUS 等状态位 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 两阶段写机制 (Two-Phase Write)
为了保证精确状态更新，CSR 寄存器的修改分为两个阶段：
1.  **Execute 阶段**: 接收 Exu 的 `exe2csr` 请求，计算新的寄存器值，将其暂存到 `csr_wdata`, `csr_idx` 等临时寄存器中。
2.  **Commit 阶段**: 只有当 ROB 发送 `rob2csr.commit` 信号时，才真正将临时寄存器的值写入 `CSR_RegFile`。
    *   原因：CSR 指令可能是推测执行的（尽管流水线通常会序列化 CSR 指令，但统一在 Commit 阶段更新是最安全的做法）。

### 3.2 异常/中断处理逻辑
CSR 模块是异常处理的大脑：
*   **中断产生**: 组合逻辑 `comb_interrupt` 实时监控 `mip`, `mie`, `mideleg` 和 `mstatus`，判断是否有符合当前特权级的中断需要响应，并发送给 ROB。
*   **异常响应**: 组合逻辑 `comb_exception` 响应 ROB 的 `rob_bcast`：
    *   **Trap Entry**: 更新 `mcause/scause` (原因), `mepc/sepc` (断点), `mtval/stval` (辅助信息)。
    *   **Stack Transfer**: 更新 `mstatus/sstatus`，保存旧的 IE (Interrupt Enable) 和 PP (Previous Privilege)，并切换到新的特权级。
    *   **Vector Jump**: 计算 `trap_pc`，如果是向量中断模式 (`mtvec[0]==1`)，则根据 Cause 跳转到偏移地址。
    *   **Trap Return**: 响应 `MRET/SRET`，从 `mepc/sepc` 恢复 PC，并恢复特权级和中断使能状态。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_csr_read` (读访问)
- **功能描述**：根据 Exu 提供的 `idx`，直接从 `CSR_RegFile` 读取数据。
- **依赖**：纯组合逻辑，Exu 需要在一个周期内通过 Bypass 拿到数据（或由 Exu 自身Latch）。由于本设计中 CSR 读写通常看作单周期，故直接输出。

### 4.2 `comb_csr_write` (写逻辑)
- **功能描述**：
  1. 如果是 Exu 写请求 (`we`)，更新 Latch 信号。
  2. 如果是 ROB 提交 (`commit`)，根据 `wcmd` (Write/Set/Clear) 修改 `CSR_RegFile`。
  3. **特殊寄存器处理**：对 `mstatus`, `mie`, `mip` 等寄存器进行掩码处理，防止写入只读位或非法位。

### 4.3 `comb_interrupt` (中断仲裁)
- **功能描述**：判断是否触发中断。
- **逻辑**：
  ```cpp
  bool pending = (mip & mask);
  bool enabled = (mie & mask);
  bool delegated = (mideleg & mask);
  bool interrupts_enabled = (privilege < TargetPriv) || (status.IE);
  // ... 综合判断产生的 interrupt_req
  ```

### 4.4 `comb_exception` (异常副作用)
- **功能描述**：处理 Trap 进入和退出。
- **优先级**：中断 > 异常。如果 ROB 同时报告了异常和收到中断响应，CSR 会优先处理中断（虽然通常 ROB 会裁决好）。

- **优先级**：中断 > 异常。如果 ROB 同时报告了异常和收到中断响应，CSR 会优先处理中断（虽然通常 ROB 会裁决好）。

### 4.5 特殊寄存器处理 (Special Handling & Masks)
部分 CSR 寄存器存在别名 (Aliasing) 关系，且写入时受掩码 (Mask) 保护：

1.  **MSTATUS / SSTATUS**:
    *   `sstatus` 是 `mstatus` 的子集（影子寄存器）。
    *   写 `mstatus` 时，保留位掩码为 `0x7f800644` (即仅部分位可写，如 MIE, MPIE, MPP 等)。
    *   写 `sstatus` 时，只能修改 S-Mode 相关位，保留位掩码为 `0x7ff21ecc`。
    *   **同步更新**: 修改其中一个，另一个会自动同步更新。

2.  **MIE / SIE (Interrupt Enable)**:
    *   `sie` 是 `mie` 的子集。
    *   写 `mie` 掩码：`0x00000bbb` (可写 M/S/U 三种模式的软/时/外中断使能)。
    *   写 `sie` 掩码：`0x00000333` (仅可写 S/U 模式的中断使能)。

3.  **MIP / SIP (Interrupt Pending)**:
    *   `sip` 是 `mip` 的子集。
    *   写 `mip` 掩码：`0x00000bbb`。
    *   写 `sip` 掩码：`0x00000333`。
    *   *注*: 硬件通常只允许软件清零某些 Pending 位（如时钟中断），具体行为遵循 RISC-V 规范。

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| *N/A* | *N/A* | *N/A* | CSR 行为属于系统事件，通常不直接计入微架构性能瓶颈，但频繁的 Trap 会导致 Frontend Bound (Fetch Latency) |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `CSR_RegFile` | `21 * 32` | `reg` array | 分别对应 MSTATUS, MEPC, MCAUSE 等物理寄存器 |
| `privilege` | 2 | `reg` | 当前特权级寄存器 (Machine/Supervisor/User) |
| `csr_wxxx` | Various | `reg` | (`csr_idx`, `csr_wdata`, `csr_wcmd`, `csr_we`) 暂存 Exu 写请求的中间寄存器 |

> [!NOTE] 信号命名约定
> 代码中后缀为 `_1` 的变量（如 `CSR_RegFile_1`, `privilege_1`）并非物理寄存器，而是**组合逻辑计算出的下一周期更新值 (Next State Wire)**。
> 在 `seq()` 函数中，这些 `_1` 信号的值会被更新到对应的无后缀寄存器中 (e.g., `privilege = privilege_1`)。

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Exception Logic | Complex | 状态机逻辑，包含大量位操作和条件判断 |
