# Idu (Instruction Decode Unit) 设计文档

## 1. 概述 (Overview)
`Idu` 位于 pre-idu queue 与 rename 之间，负责：

1. 将 `PreIduIssueIO` 中的指令译码为 `DecRenIO::DecRenInst`。
2. 为分支指令分配 `br_id`，并维护 in-flight 分支集合 `br_mask`。
3. 接收执行侧分支解析结果（`exu2id`），向下游广播 `mispred/clear_mask`。
4. 在 `ren2dec->ready` 握手与 `rob_bcast->flush` 条件下推进本地分支状态。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口 (`IduIn`)

| 信号/字段 | 位宽 | 来源 | 描述 |
| :--- | :--- | :--- | :--- |
| `issue->entries[i]` | `InstructionBufferEntry` | PreIduQueue | 待译码指令槽（含 `valid/inst/ftq/page_fault_inst`） |
| `ren2dec->ready` | 1 | Rename | rename 是否可接收本拍译码输出 |
| `rob_bcast->flush` | 1 | ROB | 全局冲刷信号（最高优先级） |
| `exu2id->mispred` | 1 | EXU | 分支误预测标记（在 `seq()` 锁存进 `br_latch`） |
| `exu2id->br_id` | `BR_TAG_WIDTH` | EXU | 已解析分支 ID |
| `exu2id->redirect_rob_idx` | `ROB_IDX_WIDTH` | EXU | 重定向对应 ROB 位置 |
| `exu2id->clear_mask` | `BR_MASK_WIDTH` | EXU | 已解析分支清理掩码 |

### 2.2 输出接口 (`IduOut`)

| 信号/字段 | 位宽 | 去向 | 描述 |
| :--- | :--- | :--- | :--- |
| `dec2ren->valid[i]` | 1 | Rename | 槽位有效 |
| `dec2ren->uop[i]` | `DecRenInst` | Rename | 译码后的 uop（含 `br_id/br_mask`） |
| `dec_bcast->mispred` | 1 | 全后端广播 | 是否误预测 |
| `dec_bcast->br_id` | `BR_TAG_WIDTH` | 全后端广播 | 误预测分支 ID |
| `dec_bcast->redirect_rob_idx` | `ROB_IDX_WIDTH` | 全后端广播 | 重定向 ROB 位置 |
| `dec_bcast->br_mask` | `BR_MASK_WIDTH` | 全后端广播 | 误预测需冲刷掩码 |
| `dec_bcast->clear_mask` | `BR_MASK_WIDTH` | 全后端广播 | 已解析分支清理掩码 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 分支状态与 Tag 生命周期
`Idu` 维护以下核心状态：

1. `tag_vec[MAX_BR_NUM]`：Tag 空闲位图（`1=空闲`，`0=占用`，其中 `tag 0` 保留）。
2. `now_br_mask`：当前 in-flight 分支集合。
3. `br_mask_cp[tag]`：每个分支 Tag 对应 checkpoint（用于 mispred 回收更年轻分支）。
4. `pending_free_mask`：延迟一拍释放集合，避免同拍回收/复用同一 `br_id`。
5. `br_latch`：上一拍锁存的 EXU 分支解析结果。

### 3.2 组合-时序两阶段更新

1. `comb_begin()`：`*_1 <- *`，建立本拍可写副本。
2. `comb_decode/comb_branch/comb_fire()`：读取当前态并写 `*_1` / 对外输出。
3. `seq()`：`*_1 -> *` 提交，并锁存 `exu2id -> br_latch`。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制状态到本拍工作副本。
- **输入依赖**：`tag_vec`, `br_mask_cp`, `now_br_mask`, `pending_free_mask`。
- **输出更新**：`tag_vec_1`, `br_mask_cp_1`, `now_br_mask_1`, `pending_free_mask_1`。
- **约束/优先级**：仅做镜像复制，不分配/释放 Tag，不驱动外部接口。

### 4.2 `comb_decode`
- **功能描述**：译码 `issue` 槽位并产生 `dec2ren`，同时为分支预分配 `alloc_tag`。
- **输入依赖**：`in.issue->entries/pc`, `tag_vec`, `max_br_per_cycle`, `br_latch.clear_mask`, `now_br_mask`。
- **输出更新**：`out.dec2ren->valid/uop`, `alloc_tag[]`。
- **约束/优先级**：
1. 每拍最多预分配 `max_br_per_cycle` 个分支 Tag。
2. `clear_mask` 先作用于 `running_mask`（已解析分支不继续传播）。
3. Tag 不足时置 `stall`，后续槽位 `valid=0`。

### 4.3 `comb_branch`
- **功能描述**：广播锁存的分支解析结果到 `dec_bcast`。
- **输入依赖**：`br_latch.{mispred, br_id, redirect_rob_idx, clear_mask}`。
- **输出更新**：`out.dec_bcast->{mispred, br_id, redirect_rob_idx, br_mask, clear_mask}`。
- **约束/优先级**：仅广播，不更新本地分支状态；本地状态统一由 `comb_fire` 更新。

### 4.4 `comb_fire`
- **功能描述**：在 flush / clear / mispred / fire 条件下推进 Tag 状态。
- **输入依赖**：`in.rob_bcast->flush`, `in.ren2dec->ready`, `out.dec2ren->valid/uop`, `alloc_tag`, `br_latch`, `now_br_mask`, `br_mask_cp`, `pending_free_mask`。
- **输出更新**：`tag_vec_1`, `now_br_mask_1`, `br_mask_cp_1`, `pending_free_mask_1`。
- **约束/优先级**：
1. `flush` 最高优先级，直接清空分支状态并返回。
2. 先处理 `pending_free_mask` 的延迟释放，再处理 `clear_mask`。
3. `mispred` 路径只回收更年轻分支，不执行新分配提交。
4. 仅对 `fire && is_branch(uop)` 的槽位提交 Tag 占用和 checkpoint。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `idu_tag_stall` | 分支 Tag 不足停顿次数 | `comb_decode` 中无可分配 `alloc_tag` 时递增 |
| `stall_br_id_cycles` | 因分支 ID 资源不足导致的周期停顿 | 与 `idu_tag_stall` 同场景统计 |

---

## 6. 资源占用 (Resource Usage)

### 6.1 持久状态资源

| 名称 | 规格 | 类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `tag_vec` | `MAX_BR_NUM` | reg array | Tag 空闲位图 |
| `now_br_mask` | `BR_MASK_WIDTH` | reg | 当前 in-flight 分支集合 |
| `br_mask_cp` | `MAX_BR_NUM * BR_MASK_WIDTH` | reg array | 分支 checkpoint |
| `pending_free_mask` | `BR_MASK_WIDTH` | reg | 延迟释放集合 |
| `br_latch` | `ExuIdIO` | reg-like latch | 锁存的分支解析结果 |

### 6.2 组合工作信号

| 名称 | 规格 | 类型 | 描述 |
| :--- | :--- | :--- | :--- |
| `tag_vec_1` | `MAX_BR_NUM` | wire array | 下一拍状态候选 |
| `now_br_mask_1` | `BR_MASK_WIDTH` | wire | 下一拍状态候选 |
| `br_mask_cp_1` | `MAX_BR_NUM * BR_MASK_WIDTH` | wire array | 下一拍状态候选 |
| `pending_free_mask_1` | `BR_MASK_WIDTH` | wire | 下一拍状态候选 |
| `alloc_tag` | `DECODE_WIDTH * BR_TAG_WIDTH` | static wire | `comb_decode` 预分配结果，供 `comb_fire` 使用 |
