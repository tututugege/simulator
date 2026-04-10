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

## 6. 资源占用 (Resource Usage)

### 6.1 持久状态资源

| 名称 | 规格 | 类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `inst_r` / `inst_valid` | `DECODE_WIDTH` | reg array | rename 流水寄存器 |
| `spec_RAT` | `ARF_NUM+1` | reg array | 推测映射表 |
| `arch_RAT` | `ARF_NUM+1` | reg array | 提交映射表 |
| `free_vec` | `PRF_NUM` | reg array | 空闲位图 |
| `spec_alloc` | `PRF_NUM` | reg array | 推测分配位图 |
| `RAT_checkpoint` | `MAX_BR_NUM*(ARF_NUM+1)` | reg array | 分支快照 |
| `alloc_checkpoint` | `MAX_BR_NUM*PRF_NUM` | reg array | 分支后分配记录 |

### 6.2 组合工作信号

| 名称 | 规格 | 类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `alloc_reg` | `DECODE_WIDTH` | static wire | 本拍目的 preg 预分配结果 |
| `fire` | `DECODE_WIDTH` | static wire | `ren->dis` 实际握手结果 |
