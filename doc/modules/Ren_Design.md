# Ren (Rename) 设计文档

## 1. 概述 (Overview)
`Ren` 模块位于 `Idu` 与 `Dispatch` 之间，负责：

1. 为源/目的寄存器完成物理寄存器重命名。
2. 维护推测态与架构态映射（`spec_RAT` / `arch_RAT`）。
3. 用 free list（FIFO）管理物理寄存器分配与回收。
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

### 3.1 核心状态

1. `spec_RAT[ARF_NUM+1]`：推测态 RAT，rename 实时查表。
2. `arch_RAT[ARF_NUM+1]`：提交态 RAT，仅 commit 更新。
3. `free_list[PRF_NUM]`：空闲 preg FIFO 环形存储。
4. `free_head`：推测分配头指针（head_spec）。
5. `free_head_commit`：提交边界头指针（head_commit）。
6. `free_tail`：回收写指针。
7. `RAT_checkpoint[MAX_BR_NUM][ARF_NUM+1]`：按 `br_id` 保存 RAT 快照。
8. `alloc_checkpoint_head[MAX_BR_NUM]`：按 `br_id` 保存分支时的 `free_head` 快照。

### 3.2 恢复策略

1. `flush`：`spec_RAT <- arch_RAT`，并执行 `free_head <- free_head_commit` 回收全部推测分配。
2. `mispred`：`spec_RAT <- RAT_checkpoint[br_id]`，并执行 `free_head <- alloc_checkpoint_head[br_id]` 回收错误路径分配。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制全部状态到 `_1` 工作副本。
- **输入依赖**：`inst_r/inst_valid`, `spec_RAT`, `arch_RAT`, `RAT_checkpoint`, free list 与 checkpoint 状态。
- **输出更新**：对应 `_1` 状态数组。
- **约束/优先级**：纯镜像复制，不做资源决策。

### 4.2 `comb_alloc`
- **功能描述**：从 free list 预选目的 preg，并计算 `ren2dis->valid`。
- **输入依赖**：`free_list/head/count`, `inst_valid`, `inst_r[].dest_en`。
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
- **输出更新**：`spec_RAT_1`, `arch_RAT_1`, free list 指针/计数、checkpoint 快照、`out.ren2dec->ready`。
- **约束/优先级**：`flush` 优先于 `mispred`；分支 `fire` 时保存 checkpoint；commit 正常提交时回收 `old_dest_preg` 并更新 `arch_RAT`。

### 4.5 `comb_pipeline`
- **功能描述**：推进 rename 流水寄存器，处理清空与背压保留。
- **输入依赖**：`in.dec2ren`, `out.ren2dec->ready`, `fire[]`, `in.rob_bcast->flush`, `in.dec_bcast->{mispred, clear_mask}`, `inst_valid/inst_r`。
- **输出更新**：`inst_valid_1[]`, `inst_r_1[]`。
- **约束/优先级**：flush/mispred 时全部置无效；保留条目与新采样条目均清除 `clear_mask`。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `stall_preg_cycles` | 因 preg 资源不足导致的停顿周期 | `comb_alloc` 分配失败时递增 |

---

## 6. 存储器类型与端口

### 6.1 `spec_RAT / arch_RAT / RAT_checkpoint`
与原设计一致：
- `spec_RAT` 提供 rename 查表并在 fire 更新。
- `arch_RAT` 在 commit 更新，并作为 flush 恢复源。
- `RAT_checkpoint` 在分支 fire 写入，在 mispred 读取恢复。

### 6.2 `free_list + 指针`
类型：环形 FIFO（空闲 preg 管理）

| 结构 | 深度/位宽 | 读端口 | 写端口 |
| :--- | :--- | :--- | :--- |
| `free_list` | `PRF_NUM x PRF_IDX_WIDTH` | `DECODE_WIDTH`（分配预读） | `COMMIT_WIDTH`（提交回收写入） |
| `free_head/free_head_commit/free_tail` | 标量 | `comb_alloc/comb_fire` | `comb_fire` |

端口分配说明：
- 分配：`comb_alloc` 预读，`comb_fire` 在 fire 时推进 `free_head`。
- 提交：commit 正常提交时写回 `old_dest_preg` 到 `free_tail`，并推进 `free_head_commit`。
- 空闲数量：按 `distance(free_head, free_tail)` 动态计算，不持久化存储。
- flush：`free_head <- free_head_commit`。
- mispred：`free_head <- alloc_checkpoint_head[br_id]`。

### 6.3 `alloc_checkpoint_head`
类型：寄存器堆（分支分配边界快照）

| 结构 | 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- | :--- |
| `alloc_checkpoint_head` | `MAX_BR_NUM` | `1`（按 `br_id`） | `DECODE_WIDTH`（分支 fire） |

端口分配说明：
- 分支 fire 时记录 `free_head`。
- mispred 时读取并恢复。

---

## 7. Assert 断言点

- `Assert(spec_head/commit_head/tail in [0, PRF_NUM))`：
  - 时机：`comb_alloc` 前和 `comb_fire` 末尾的一致性检查。
  - 保护目标：防止环形指针越界导致 free list 读写到非法槽位。
  - 影响范围：分配、提交回收、flush/mispred 恢复全路径。

- `Assert(free_slots + pending_spec == PRF_NUM - (ARF_NUM + 1))`：
  - 时机：`ren_freelist_sanity()`。
  - 保护目标：确保“可分配池总量守恒”，防止丢寄存器或重复回收。
  - 影响范围：重命名资源耗尽判断与恢复后资源一致性。

- `Assert(free_head_1 != free_tail_1)`（分配时）：
  - 时机：`comb_fire` 中处理 `fire && dest_en` 前。
  - 保护目标：防止空队列分配（underflow）。
  - 影响范围：目的寄存器分配与后续 RAT 更新正确性。

- `Assert(br_id < MAX_BR_NUM)`（分支 checkpoint / mispred 恢复）：
  - 时机：分支 fire 保存 checkpoint、mispred 恢复入口。
  - 保护目标：防止 checkpoint 表越界访问。
  - 影响范围：分支恢复路径与 RAT/free list 快照一致性。

- `Assert(free_head_commit_1 != free_head_1)`（commit 回收时）：
  - 时机：`commit && dest_en && !exception && !interrupt` 回收前。
  - 保护目标：防止“无待提交分配”情况下错误推进 commit 边界。
  - 影响范围：flush 恢复边界（`head_commit`）准确性。
