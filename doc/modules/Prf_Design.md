# Prf (Physical Register File) 设计文档

## 1. 概述 (Overview)
Prf (Physical Register File) 模块是后端数据流的汇聚点。它不仅负责存储物理寄存器的值，还集成了**多级旁路 (Multi-Level Bypass)** 网络，支持在 **Issue 阶段**直接读取最新产生的数据（即使这些数据尚未写入 SRAM）。此外，Prf 还负责在写回阶段 (Writeback) 检测分支误预测并触发流水线冲刷。

## 2. 接口定义 (Interface Definition)

### 2.1 核心输入接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `iss2prf.iss_entry` | `ISSUE_WIDTH * sizeof(Entry)` | 输入 | Isu | 发射阶段的指令请求（包含源 Preg 索引） |
| `exe2prf.entry` | `ISSUE_WIDTH * sizeof(Entry)` | 输入 | Exu | 执行完成的指令（写回流水线输入） |
| `exe2prf.bypass` | `TOTAL_FU * sizeof(Entry)` | 输入 | Exu | 来自所有功能单元的运算结果广播 |

### 2.2 核心输出接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `prf2exe.iss_entry` | `ISSUE_WIDTH * sizeof(Entry)` | 输出 | Exu | 读出操作数后的指令包 |
| `prf2dec.mispred` | 1 | 输出 | Idu/Ren | 误预测信号 |
| `prf2dec.redirect_pc` | 32 | 输出 | Idu | 误预测重定向目标地址 |
| `prf_awake` | `LSU_WIDTH * sizeof(Wake)` | 输出 | Isu/Ren | Load 写回唤醒信号 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 物理寄存器堆 (Register File)
- **实现方式**: 多端口 RAM (SRAM 或 Register Array)，采用 `reg<32>` 数组模拟。
- **读端口 (Read Ports)**: `(ISSUE_WIDTH - STA_WIDTH) * 2 + STA_WIDTH * 1`。
    - 大部分运算指令需要读取 2 个源操作数。
    - Store Address (STA) 微操作仅需读取 1 个基址寄存器 (Base)。
- **写端口 (Write Ports)**: `IQ_INT_PORTS + IQ_LD_PORTS`。
    - 仅 `IQ_INT` (ALU, MUL, DIV, CSR) 和 `IQ_LD` (Load) 的发射端口对应指令需要写回。
    - `IQ_STA` (Store Addr), `IQ_STD` (Store Data) 和 `IQ_BR` (Branch) 对应的发射端口无需写回。

### 3.2 多级旁路网络 (Bypass Network)
为了实现背靠背执行，Prf 在 `comb_read` 阶段会并行检查以下数据源，优先级从高到低：
1.  **Exe Bypass**: 来自本周期功能单元刚刚计算出的结果 (`exe2prf.bypass`)。
    *   *场景*：指令 A 在 cycle T 执行完毕，指令 B 在 cycle T 发射，B 可以直接拿到 A 的结果。
2.  **Writeback Bypass**: 来自上一周期执行完毕、本周期正在写回的数据 (`inst_r`)。
    *   *场景*：指令 A 在 cycle T-1 执行完毕，cycle T正在写寄存器堆；指令 B 在 cycle T 发射。
3.  **Register File**: 从 SRAM 中读取的稳定数据。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_read` (操作数读取)
- **功能描述**：为 **Issue (发射)** 阶段的指令读取源操作数。
- **逻辑**：实现 3.2 节描述的旁路选择逻辑。对于每个源操作数，并行比较 `src_preg` 与 Bypass 来源的 `dest_preg`。
- **时序**：在指令从 Issue Queue 发射到 Execute Unit 的过程中进行（Payload 读）。

### 4.2 `comb_br_check` (分支检查)
- **功能描述**：在 Writeback 阶段 (`inst_r`) 检查是否有指令被标记为误预测 (`mispred`)。
- **仲裁**：如果同一周期有多条分支误预测，选择**最老 (Oldest)** 的一条（根据 ROB ID 比较）作为重定向基准，生成 `redirect_pc` 和 `br_tag`。


### 4.4 `comb_awake` (Load 唤醒)
- **功能描述**：处理 Load 指令的唤醒。
- **特殊处理**：这部分逻辑虽然在 Prf 中，但主要服务于 Lsu 的 Cache Miss/Hit 唤醒。它会过滤掉被 Squash 的指令，将有效的 Load 结果对应的 Preg 广播给 Isu。

### 4.5 `comb_write` (寄存器写入)
- **功能描述**：将 `inst_r` 中的结果实际写入 `reg_file` 数组。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `branch_mispred`| 分支误预测次数 | Bad Speculation | 统计在后端检测到的实际误预测次数 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `reg_file` | `PRF_NUM * 32` | `reg` array | 物理寄存器堆主体 |
| `inst_r` | `ISSUE_WIDTH * sizeof(Entry)` | `reg` | 写回级流水线寄存器 (Writeback Latch) |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| Read Ports | `(ISSUE_WIDTH - STA_WIDTH) * 2 + STA_WIDTH * 1` | 寄存器堆读端口 (Store Address 仅需读 1 个操作数) |
| Write Ports | `IQ_INT_PORTS + IQ_LD_PORTS` | 寄存器堆写端口 (仅 IQ_INT 和 IQ_LD 对应端口需写回) |
| Bypass Muxes | `ISSUE_WIDTH * 2 * Sources` | 操作数选择多路复用器（规模巨大） |
| Age Comparator | `BRU_NUM` | 用于选择最早误预测分支的比较器树 (仅需比较 BRU 输出) |
