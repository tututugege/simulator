# Ren (Rename) 设计文档

## 1. 概述 (Overview)
`Ren` 模块位于 `Idu` 与 `Dispatch` 之间，负责：

1. 为源/目的寄存器完成物理寄存器重命名。
2. 管理推测态与架构态映射（`spec_RAT` / `arch_RAT`）。
3. 维护 freelist（`free_vec`）与推测分配位图（`spec_alloc`）。
4. 在分支误预测与全局 flush 时执行快速恢复。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口 (`RenIn`)

| 信号/字段 | 位宽 | 来源 | 描述 |
| :--- | :--- | :--- | :--- |
| `dec2ren->uop[i]` | `DecRenInst` | Idu | 待重命名指令 |
| `dec2ren->valid[i]` | 1 | Idu | 槽位有效 |
| `dis2ren->ready` | 1 | Dispatch | 下游是否可接收 |
| `dec_bcast->mispred` | 1 | Idu | 分支误预测广播 |
| `dec_bcast->br_id` | `BR_TAG_WIDTH` | Idu | 误预测分支 ID |
| `dec_bcast->clear_mask` | `BR_MASK_WIDTH` | Idu | 已解析分支清理掩码 |
| `rob_bcast->flush` | 1 | ROB | 全局冲刷 |
| `rob_commit->commit_entry[i]` | `InstEntry` | ROB | 提交信息（更新 arch_RAT、回收旧 preg） |

### 2.2 输出接口 (`RenOut`)

| 信号/字段 | 位宽 | 去向 | 描述 |
| :--- | :--- | :--- | :--- |
| `ren2dec->ready` | 1 | Idu | 上游反压信号 |
| `ren2dis->uop[i]` | `RenDisInst` | Dispatch | 重命名后的 uop |
| `ren2dis->valid[i]` | 1 | Dispatch | 槽位有效 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 映射与分配状态

1. `spec_RAT[ARF_NUM+1]`：推测态 RAT，供 rename 实时查表。
2. `arch_RAT[ARF_NUM+1]`：提交态 RAT，仅在 commit 更新。
3. `free_vec[PRF_NUM]`：物理寄存器空闲位图。
4. `spec_alloc[PRF_NUM]`：推测路径分配记录。
5. `RAT_checkpoint[MAX_BR_NUM][ARF_NUM+1]`：按 `br_id` 保存的 RAT 快照。
6. `alloc_checkpoint[MAX_BR_NUM][PRF_NUM]`：按 `br_id` 记录分支后新分配 preg。

### 3.2 恢复策略

1. `flush`：`spec_RAT <- arch_RAT`，并回收当前推测分配。
2. `mispred`：从 `RAT_checkpoint[br_id]` 恢复 `spec_RAT`，并通过 `alloc_checkpoint[br_id]` 回收错误路径分配。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制全部状态到 `_1` 工作副本。
- **输入依赖**：`inst_r/inst_valid`, `spec_RAT`, `arch_RAT`, `free_vec`, `spec_alloc`, `RAT_checkpoint`, `alloc_checkpoint`。
- **输出更新**：对应 `_1` 状态数组。
- **约束/优先级**：纯镜像复制，不做资源决策。

### 4.2 `comb_alloc`
- **功能描述**：从 `free_vec` 预选目的 preg，并计算 `ren2dis->valid`。
- **输入依赖**：`free_vec`, `inst_valid`, `inst_r[].dest_en`。
- **输出更新**：`alloc_reg[]`, `out.ren2dis->valid[]`。
- **约束/优先级**：按槽位顺序分配；前序目的寄存器分配失败会阻塞后续槽位。

### 4.3 `comb_rename`
- **功能描述**：完成源/目的 preg 绑定，并处理同拍 RAW/WAW 旁路。
- **输入依赖**：`inst_r/inst_valid`, `spec_RAT`, `alloc_reg`, 同拍前序槽位输出 `dest_preg`。
- **输出更新**：`out.ren2dis->uop[].{src1_preg, src2_preg, old_dest_preg, dest_preg}`。
- **约束/优先级**：旁路优先于常规查表；x0 写入不参与旁路。

### 4.4 `comb_fire`
- **功能描述**：根据握手提交重命名状态、处理 commit、并执行 flush/mispred 恢复。
- **输入依赖**：`out.ren2dis->valid`, `in.dis2ren->ready`, `in.rob_commit`, `in.rob_bcast`, `in.dec_bcast`, `inst_r`, 各类映射与 checkpoint 状态。
- **输出更新**：`spec_RAT_1`, `arch_RAT_1`, `free_vec_1`, `spec_alloc_1`, `RAT_checkpoint_1`, `alloc_checkpoint_1`, `out.ren2dec->ready`。
- **约束/优先级**：`flush` 优先于 `mispred`；分支 `fire` 时保存 checkpoint；commit 回收 `old_dest_preg` 并更新 `arch_RAT`。

### 4.5 `comb_pipeline`
- **功能描述**：推进 rename 流水寄存器，处理清空与背压保留。
- **输入依赖**：`in.dec2ren`, `out.ren2dec->ready`, `fire[]`, `in.rob_bcast->flush`, `in.dec_bcast->{mispred, clear_mask}`, `inst_valid/inst_r`。
- **输出更新**：`inst_valid_1[]`, `inst_r_1[]`。
- **约束/优先级**：flush/mispred 时全部置无效；保留条目与新采样条目均清除 `clear_mask`。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `ren_reg_stall` | 物理寄存器不足停顿次数 | `comb_alloc` 分配失败时递增 |
| `stall_preg_cycles` | 因 preg 资源不足导致的停顿周期 | 与 `ren_reg_stall` 同场景 |

---

## 6. 存储器类型与端口

> 说明：`*_1` 是 next-state 工作副本（组合阶段写、`seq` 提交），不代表额外硬件端口；端口统计按模块行为语义给出。

### 6.1 Rename 流水寄存器（`inst_r` / `inst_valid`）
类型：寄存器堆（按槽位保存 in-flight rename 输入）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `DECODE_WIDTH` | `1`（按槽位读取） | `1`（按槽位写回） |

端口分配说明：
- 读口：`comb_alloc/comb_rename/comb_fire` 读取当前槽位。
- 写口：`comb_pipeline` 在 `ready` 时采样新输入，背压时保留未 fire 槽位，`seq` 提交。

### 6.2 `spec_RAT`
类型：寄存器堆（推测映射表）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ARF_NUM + 1` | `3 * DECODE_WIDTH`（`src1/src2/old_dest`） | `DECODE_WIDTH`（steady-state） |

端口分配说明：
- 读口：`comb_rename` 对每槽位读取 `src1/src2/dest` 三路映射。
- 写口：`comb_fire` 对 fire 且写回目的寄存器的指令更新 `spec_RAT_1[dest_areg]`。
- 恢复路径：`flush` 用 `arch_RAT` 覆盖，`mispred` 用 `RAT_checkpoint[br_id]` 覆盖。

### 6.3 `arch_RAT`
类型：寄存器堆（提交映射表）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ARF_NUM + 1` | `1`（flush 恢复读取） | `COMMIT_WIDTH` |

端口分配说明：
- 写口：commit 时对 `dest_areg` 写入 `dest_preg`。
- 读口：`flush` 路径把 `arch_RAT_1` 拷贝回 `spec_RAT_1`。

### 6.4 `free_vec`
类型：位图寄存器堆（freelist）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `PRF_NUM` | `1`（扫描读取） | `DECODE_WIDTH + COMMIT_WIDTH`（steady-state） |

端口分配说明：
- 读口：`comb_alloc` 顺序扫描可用 preg。
- 写口（steady-state）：fire 分配时清零新 `dest_preg`；commit 时释放 `old_dest_preg`。
- 恢复路径：`flush/mispred` 会按位图批量回收错误路径分配。

### 6.5 `spec_alloc`
类型：位图寄存器堆（推测分配记录）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `PRF_NUM` | `1` | `DECODE_WIDTH + COMMIT_WIDTH`（steady-state） |

端口分配说明：
- 写口（steady-state）：fire 分配时置位 `dest_preg`；commit 正常提交时清除对应 `dest_preg` 的推测标记。
- 恢复路径：`flush/mispred` 根据快照批量修正 `spec_alloc`。

### 6.6 `RAT_checkpoint`
类型：寄存器堆（分支 RAT 快照）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `MAX_BR_NUM * (ARF_NUM + 1)` | `1`（按 `br_id` 读整行） | `DECODE_WIDTH`（按分支 fire 次数） |

端口分配说明：
- 写口：分支 fire 时，将当前 `spec_RAT_1` 保存到对应 `br_id` 行。
- 读口：`mispred` 时读取 `RAT_checkpoint[br_id]` 恢复 `spec_RAT_1`。

### 6.7 `alloc_checkpoint`
类型：寄存器堆（分支后分配记录）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `MAX_BR_NUM * PRF_NUM` | `1`（按 `br_id` 读整行） | `DECODE_WIDTH + MAX_BR_NUM`（steady-state） |

端口分配说明：
- 写口 A：每条 fire 且 `dest_en` 的指令，将 `dest_preg` 在所有分支行标记为已分配。
- 写口 B：分支 fire 时清空该 `br_id` 行，作为新的分支起点快照。
- 读口：`mispred` 时读取 `alloc_checkpoint[br_id]`，回收错误路径上新分配的 preg。
