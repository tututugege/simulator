# Idu (Instruction Decode Unit) 设计文档

## 1. 概述 (Overview)
Idu 模块是后端流水线的入口，主要负责将取指阶段获得的指令流并行译码为微指令 (MicroOps)。此外，Idu 还承担了分支标签 (Branch Tag) 的分配与回收、FTQ 条目管理以及与前端的流控握手。

## 2. 接口定义 (Interface Definition)

### 2.1 前端接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `front2dec.inst` | `FETCH_WIDTH * 32` | 输入 | 前端 | 原始指令机器码 |
| `front2dec.pc` | `FETCH_WIDTH * 32` | 输入 | 前端 | 指令 PC 及其偏移 |
| `front2dec.valid` | `FETCH_WIDTH * 1` | 输入 | 前端 | 指令有效位 |
| `dec2front.ready` | 1 | 输出 | 前端 | 后端就绪信号（反压前端） |
| `dec2front.fire` | `FETCH_WIDTH * 1` | 输出 | 前端 | 指令确认接收信号 |

### 2.2 后端流水线接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dec2ren.uop` | `FETCH_WIDTH * sizeof(Uop)` | 输出 | Rename | 译码后的微指令序列 |
| `dec2ren.valid` | `FETCH_WIDTH * 1` | 输出 | Rename | 传递给 Rename 的有效位 |
| `ren2dec.ready` | 1 | 输入 | Rename | Rename 阶段就绪信号 |
| `prf2dec.mispred` | 1 | 输入 | PRF/BRU | 分支误预测信号 |
| `prf2dec.br_tag` | `BR_TAG_WIDTH` | 输入 | PRF/BRU | 误预测分支的标签 |
| `rob_bcast.flush` | 1 | 输入 | ROB | 流水线冲刷信号 |
| `commit.commit_entry` | `COMMIT_WIDTH * sizeof(Entry)` | 输入 | ROB | 指令提交信息（用于 Tag 回收） |

### 2.3 广播与反馈接口

| 信号/字段 | 位宽 (bits) | 方向 | 来源/去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `dec_bcast.mispred`| 1 | 输出 | 后端广播 | Idu 内部误预测确认 |
| `dec_bcast.br_mask` | `BR_MASK_WIDTH` | 输出 | 后端广播 | 需要被冲刷的分支掩码 |
| `dec_bcast.br_tag` | `BR_TAG_WIDTH` | 输出 | 后端广播 | 误预测跳转的分支标签 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 分支标签管理 (Branch Tag Management)
IDU 内部维护了一个循环队列 `tag_list` 和一个空闲比特向量 `tag_vec`，用于管理推测执行的分支：
- **分配逻辑**：指令进入时分配 `now_tag`。如果识别到分支指令，则从 `tag_vec` 中取出一个可用 Tag 作为该分支的唯一标识。
- **层级掩码**：后端模块通过 `br_mask` 进行层级控制，能够在一个周期内识别并撤销所有由特定分支触发的推测指令。
- **回收机制**：当分支指令在 ROB 队头成功提交时，回收其占用的 Tag 重新标记为可用。

### 3.2 FTQ 条目管理
每个取指块在译码阶段会对应一个 FTQ 条目索引（`ftq_idx`），并标记当前指令是否为块内最后一条（`ftq_is_last`），这对于后续的提交和预测训练至关重要。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_decode` (译码与 Tag 分配)
- **功能描述**：并发调用译码逻辑，将原始指令解析为 Uops。同时为分支指令分配新的 `br_tag`。
- **关键控制**：检测 FTQ 是否已满或 Tag 资源是否耗尽，若发生则触发 Stall 反压前端。

### 4.2 `comb_branch` (分支误预测处理)
- **功能描述**：当接收到 `prf2dec->mispred` 时，根据误预测分支的 Tag 查找 `tag_list`。
- **关键信号**：生成 `dec_bcast->br_mask`，标定所有受影响需要冲刷的分支，并回滚 Tag 分配指针。

### 4.3 `comb_fire` (流控握手)
- **功能描述**：综合上游取指有效性、下游 Rename 准备状态及内部冲刷/Stall 状态，决定 `fire` 信号。

### 4.4 `comb_flush` (全流水冲刷)
- **功能描述**：处理来自 ROB 的 `flush` 信号。全面清空分支标签状态池，复位 FTQ 指针。

### 4.5 `comb_release_tag` (Tag 回收)
- **功能描述**：监听 ROB 提交阶段。对于已成功提交的分支指令，将其占用的物理 Tag 资源标记回 `tag_vec` 的空闲状态。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 指标含义 | TMA 层级 | 描述 |
| :--- | :--- | :--- | :--- |
| `idu_tag_stall` | 分支标签耗尽导致暂停 | Frontend Bound | 统计因推测分支超过 `MAX_BR_NUM` 导致的流水线暂停比例 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 存储资源 (Storage Resources)
记录模块内部定义的持久化状态元素（对应代码中的 `reg` 类型变量）。

| 寄存器/存储名称 | 规格 (Size/Bits) | 硬件类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `tag_list` | `MAX_BR_NUM * BR_TAG_WIDTH` | `reg` array | 记录分支分配顺序的循环队列 |
| `tag_vec` | `MAX_BR_NUM * 1` | `reg` vector | 分支标签空闲状态位图 (1=空闲) |
| `enq_ptr` | `BR_TAG_WIDTH` | `reg` | 分支标签入队指针 |
| `now_tag` | `BR_TAG_WIDTH` | `reg` | 当前正在分配的分支标签 |

### 6.2 硬件开销 (Hardware Overhead)

| 资源名称 | 规格 (Ports/Width) | 描述 |
| :--- | :--- | :--- |
| FTQ Interface | `1 * Entry` | 每周期向 FTQ 申请/更新一个取指块的条目 |
| Decode Width | `FETCH_WIDTH` | 每周期可并行处理的最大译码指令数 |
| Tag Mask Logic | `1 * BR_MASK_WIDTH` | 误预测时用于并行冲刷的分支掩码生成逻辑 |
