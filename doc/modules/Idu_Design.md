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
| `issue->pc[i]` | 32 | PreIduQueue | 对应槽位 PC |
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

## 6. 存储器类型与端口


### 6.1 `tag_vec[MAX_BR_NUM]`
类型：寄存器堆（Tag 空闲位图，1 bit/entry）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `MAX_BR_NUM` | `1`（整向量读取） | `1`（整向量写回） |

端口分配说明：
- `comb_decode` 读取 `tag_vec` 寻找可分配 Tag。
- `comb_fire` 在 flush / clear / mispred / fire 路径更新 `tag_vec_1`，`seq` 提交。

### 6.2 `br_mask_cp[MAX_BR_NUM]`
类型：寄存器堆（checkpoint 表）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `MAX_BR_NUM` | `1`（按 `br_id` 索引读） | `1`（按索引写） |

端口分配说明：
- 读口：`mispred` 路径读取 `br_mask_cp[br_latch.br_id]` 计算回收集合。
- 写口：新分支 fire 时写入 `br_mask_cp_1[new_tag]`。
- 控制路径写：`clear_mask` 会对所有 checkpoint 执行按位清零（广播式更新）。

### 6.3 `now_br_mask` / `pending_free_mask`
类型：寄存器（标量状态）

| 存储 | 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- | :--- |
| `now_br_mask` | `1` | `1` | `1` |
| `pending_free_mask` | `1` | `1` | `1` |

端口分配说明：
- `now_br_mask` 在 decode 参与生成 `running_mask`，在 fire 中被 clear/mispred/new-branch 更新。
- `pending_free_mask` 在 fire 中先读出 `matured_free`，再写回新的延迟释放集合。

### 6.4 `br_latch`
类型：寄存器（分支解析结果锁存）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `1` | `1` | `1` |

端口分配说明：
- 读口：`comb_decode/comb_branch/comb_fire` 读取 `br_latch`（`mispred/br_id/clear_mask`）。
- 写口：`comb_fire` 将 `in.exu2id` 写入 `br_latch_1`，`seq` 提交。
