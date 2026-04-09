# Ren (Rename) 设计文档

## 1. 概述
`Ren` 模块位于 `Idu` 与 `Dispatch` 之间，负责：

1. 为源/目的寄存器完成物理寄存器重命名。
2. 管理推测态与架构态映射（`spec_RAT` / `arch_RAT`）。
3. 维护 freelist（`free_vec`）与推测分配位图（`spec_alloc`）。
4. 在分支误预测与全局 flush 时执行快速恢复。

---

## 2. 接口定义
### 2.1 输入接口

| 信号/字段 | 位宽 | 来源 | 描述 |
| :--- | :--- | :--- | :--- |
| `dec2ren->uop[i]` | `DecRenInst` | Idu | 待重命名指令 |
| `dec2ren->valid[i]` | 1 | Idu | 槽位有效 |
| `dis2ren->ready` | 1 | Dispatch | 下游是否可接收 |
| `dec_bcast->mispred` | 1 | Idu | 分支误预测广播 |
| `dec_bcast->br_id` | `BR_TAG_WIDTH` | Idu | 误预测分支 ID |
| `dec_bcast->clear_mask` | `BR_MASK_WIDTH` | Idu | 已解析分支清理掩码 |
| `rob_bcast->flush` | 1 | ROB | 全局冲刷 |
| `rob_commit->commit_entry[i]` | `RobCommitIO::RobCommitEntry` | ROB | 提交信息（更新 arch_RAT、回收旧 preg） |

### 2.2 输出接口

| 信号/字段 | 位宽 | 去向 | 描述 |
| :--- | :--- | :--- | :--- |
| `ren2dec->ready` | 1 | Idu | 上游反压信号 |
| `ren2dis->uop[i]` | `RenDisInst` | Dispatch | 重命名后的 uop |
| `ren2dis->valid[i]` | 1 | Dispatch | 槽位有效 |

---

## 3. 微架构设计
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

### 3.3 基于 `br_id` 的 Checkpoint 恢复机制

该机制的核心是维护每个 in-flight 分支对应的两个恢复快照：
1. `RAT_checkpoint[br_id]`：该分支建立时的 RAT 视图。
2. `alloc_checkpoint[br_id]`：该分支之后新分配过的 preg 集合。

机制流程如下：

1. 分支建立（checkpoint 生成）  
- 当分支指令 `fire` 时，用其 `br_id` 记录当前映射到 `RAT_checkpoint[br_id]`。  
- 同时把该 `br_id` 的 `alloc_checkpoint[br_id][*]` 清零，作为“该分支之后新增分配”的空集合起点。

2. 推测分配传播（集合累积）  
- 后续任何 `fire` 且 `dest_en` 的指令分配了 `dest_preg`，都会把该 preg 标到所有活动分支的 `alloc_checkpoint[*][dest_preg]`。  
- 含义：每个分支都持续追踪“从我开始到现在，新分配过哪些寄存器”。

3. 误预测恢复（按分支局部回滚）  
- 收到 `mispred + br_id` 时：  
  `spec_RAT` 回到 `RAT_checkpoint[br_id]`，保证映射精确回到该分支建立点；  
  `free_vec/spec_alloc` 按 `alloc_checkpoint[br_id]` 回收错误路径新增 preg。  
- 结果是只回滚该分支错误路径，不影响更老正确路径状态。

4. 全局 flush 恢复（架构态回滚）  
- `flush` 不走分支局部集合，而是直接回到 `arch_RAT` 并清空推测分配痕迹。  
- 这对应“抛弃全部推测态”，重建到提交态基线。

5. 优先级与一致性  
- 恢复优先级为 `flush > mispred`。  
- `br_id` 与两个 checkpoint 结构共同保证：恢复范围由分支边界精确界定，而不是靠遍历流水条目推断。

---

## 4. 组合逻辑功能描述
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

## 5. 性能计数器
| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `ren_reg_stall` | 物理寄存器不足停顿次数 | `comb_alloc` 分配失败时递增 |
| `stall_preg_cycles` | 因 preg 资源不足导致的停顿周期 | 与 `ren_reg_stall` 同场景 |

---

## 6. 资源占用
| 名称 | 规格 | 类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `inst_r` / `inst_valid` | `DECODE_WIDTH` | reg array | rename 流水寄存器 |
| `spec_RAT` | `ARF_NUM+1` | reg array | 推测映射表 |
| `arch_RAT` | `ARF_NUM+1` | reg array | 提交映射表 |
| `free_vec` | `PRF_NUM` | reg array | 空闲位图 |
| `spec_alloc` | `PRF_NUM` | reg array | 推测分配位图 |
| `RAT_checkpoint` | `MAX_BR_NUM*(ARF_NUM+1)` | reg array | 分支快照 |
| `alloc_checkpoint` | `MAX_BR_NUM*PRF_NUM` | reg array | 分支后分配记录 |
