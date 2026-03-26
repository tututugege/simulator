# Ren (Rename) 设计文档

## 1. 概述 (Overview)
Rename (寄存器重命名) 模块是实现乱序执行的关键步骤。它通过将架构寄存器 (Architectural Registers) 映射到更大容量的物理寄存器 (Physical Registers)，消除指令间的伪相关 (WAW, WAR)。本模块实现了完整的检查点 (Checkpoint) 机制，支持单周期内从分支预测错误中恢复架构状态。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dec2ren.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输入 | IDU | 待重命名的指令流 |
| `dec2ren.valid` | `FETCH_WIDTH * 1` | 输入 | IDU | 译码指令有效位 |
| `dis2ren.ready` | 1 | 输入 | Dispatch | 后端 Dispatch 阶段就绪信号 |
| `prf_awake` / `iss_awake` | - | 输入 | PRF/Issue | 唤醒总线，用于更新物理寄存器就绪状态 (Busy Table) |
| `dec_bcast.mispred` | 1 | 输入 | IDU | 分支误预测广播信号 |
| `rob_bcast.flush` | 1 | 输入 | ROB | 流水线冲刷信号（异常/中断） |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `ren2dec.ready` | 1 | 输出 | IDU | Rename 阶段就绪，允许 IDU 发射新指令 |
| `ren2dis.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输出 | Dispatch | 完成重命名的指令流 |
| `ren2dis.valid` | `FETCH_WIDTH * 1` | 输出 | Dispatch | 传递给 Dispatch 的有效位 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 双重映射表机制 (Dual-RAT)
为了平衡乱序执行速度与例外恢复的精确性，Ren 模块维护了两套映射表：
- **Speculative RAT (spec_RAT)**: 记录流水线前端最新的重命名状态。每周期随指令重命名动态更新，支取指令使用此表进行映射。
- **Architectural RAT (arch_RAT)**: 记录已提交（Committed）的指令映射状态。仅在指令成功退休（Commit）时更新，代表了处理器的精确架构状态。

### 3.2 恢复机制 (Recovery Mechanism)
本项目实现了两级恢复策略，以应对不同类别的流水线清空：

#### 3.2.1 分支检查点恢复 (Checkpoint)
- **保存**：在 `comb_fire` 阶段，如果流水线中有分支指令通过，Ren 模块会为该分支分配一个 `tag`，并将其当前的 `spec_RAT` 完整拷贝到 `RAT_checkpoint[tag]`。同时，`alloc_checkpoint[tag]` 开始追踪该分支之后分配的所有物理寄存器。
- **恢复**：一旦后端判定分支误预测（`mispred`），Ren 模块在下一周期从对应的 `RAT_checkpoint` 恢复 `spec_RAT`，并通过 `alloc_checkpoint` 快速释放错误路径上分配的所有物理寄存器。

#### 3.2.2 架构回滚 (Flush)
- 当发生异常或中断引发 `flush` 时，`spec_RAT` 直接从 `arch_RAT`（提交状态）恢复。由于此时 `arch_RAT` 始终指向最新的正确状态，这种方式确保了例外处理的原子性。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_rename` (重命名寻址)
- **功能描述**：根据 `spec_RAT` 查找指令的源寄存器映射。
- **关键信号处理**：处理同一周期内多条指令间的寄存器依赖（Bypass 逻辑）。如果 Packet 内指令 B 依赖指令 A，则 B 的源寄存器将直接使用 A 刚刚分配的物理寄存器，而非查询旧的 `spec_RAT`。

### 4.2 `comb_alloc` (物理寄存器分配)
- **功能描述**：从 `free_vec`（空闲列表）中为当前 Packet 内所有需要写回的指令查找并分配空闲物理寄存器。
- **关键信号**：如果空闲寄存器不足，产生 `stall` 信号反压 IDU。

### 4.3 `comb_fire` (状态更新与快照)
- **功能描述**：确认指令成功进入下一步骤。
- **关键操作**：
  - 更新 `spec_RAT`。
  - 设置新分配寄存器的 `busy_table` 位（设为 Busy）。
  - **Checkpointing**：若为分支，执行 3.2.1 节所述的快照保存。

### 4.4 `comb_branch` & `comb_flush`
- **功能描述**：分别实现基于 Checkpoint 的快速恢复和基于 Architectural RAT 的全局回退，详见第 3.2 节。

### 4.5 `comb_commit` (资源释放)
- **功能描述**：指令提交时，更新 `arch_RAT`。
- **关键操作**：将该指令对应的 **旧目的物理寄存器 (old_dest_preg)** 标记回 `free_vec`，真正完成资源的物理释放。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `ren_reg_stall` | 物理寄存器耗尽导致的暂停 | Backend Bound / Core Bound | 统计由于 PRF (Physical Register File) 记录数不足导致的流水线阻塞 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `spec_RAT` | `(ARF_NUM+1) * PRF_IDX_WIDTH` | `reg` | 指令流实时重命名使用的推测映射表 |
| `arch_RAT` | `(ARF_NUM+1) * PRF_IDX_WIDTH` | `reg` | 记录已提交指令状态的架构映射表 |
| `RAT_checkpoint` | `MAX_BR_NUM * (ARF_NUM+1) * PRF_IDX_WIDTH` | `reg` array | 分支快照存储池 |
| `free_vec` | `PRF_NUM * 1` | `reg` vector | 物理寄存器物理空闲状态位图 |
| `busy_table` | `PRF_NUM * 1` | `reg` vector | 物理寄存器结果就绪状态位图 |
| `alloc_checkpoint`| `MAX_BR_NUM * PRF_NUM * 1` | `reg` array | 记录分支后分配的寄存器掩码，用于误预测回收 |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Rename Width | `FETCH_WIDTH` | 每周期支持同时重命名的指令数量 |
| RAT Read Ports | `FETCH_WIDTH * 2` | 每周期支持的读取端口（源操作数 1/2） |
| RAT Write Ports| `FETCH_WIDTH * 1` | 每周期支持的更新端口（目的寄存器） |
| PRF Free Search| `FETCH_WIDTH` entries/cycle | 每周期从 free_vec 中搜索空闲寄存器的逻辑复杂度 |
