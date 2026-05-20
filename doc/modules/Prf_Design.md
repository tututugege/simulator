# Prf (Physical Register File) 设计文档

## 1. 概述 (Overview)
`Prf` 位于 `Isu` 与 `Exu`/写回之间，核心职责：

1. 为发射条目读取源操作数，并向 FTQ 发起 PC 查询。
2. 在读阶段提供写回级与执行级旁路。
3. 接收写回条目并更新物理寄存器堆。
4. 生成 Load 写回唤醒广播。
5. 维护一拍写回流水寄存器并处理 flush/mispred。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `iss2prf->iss_entry[]` | Isu | 发射请求（含源/目的 preg 与控制字段） |
| `exe2prf->entry[]` | Exu | 写回流水输入（上一执行阶段完成结果） |
| `exe2prf->bypass[]` | Exu | 本拍 FU 完成广播（快速旁路源） |
| `dec_bcast->{mispred,br_mask,clear_mask}` | Idu/Exu 广播链路 | 分支杀伤与分支位清理 |
| `rob_bcast->flush` | ROB | 全局冲刷 |
| `ftq_prf_pc_resp` | FTQ | PC查询结果与分支预测信息 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `prf2exe->iss_entry[]` | Exu | 已补齐 `src*_rdata` 与 `pc` 等预测信息的发射条目 |
| `prf_awake->wake[]` | Isu/Rename | Load 写回唤醒广播 |
| `ftq_prf_pc_req` | FTQ | 发起的 PC 查询请求 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 状态组织

1. `reg_file[PRF_NUM]`：物理寄存器堆当前态。
2. `inst_r[ISSUE_WIDTH]`：写回流水寄存器（上一拍 EXU 完成条目）。
3. `_1` 副本用于组合阶段累积下一拍状态。

### 3.2 读旁路优先级
`comb_read()` 中每个源操作数按以下顺序解析：

1. `reg_file[preg]` 基值。
2. `inst_r` 写回级旁路覆盖。
3. `exe2prf->bypass` 执行级旁路再次覆盖（优先级最高）。

### 3.3 写回与唤醒

1. `comb_write()` 将 `inst_r` 结果写入 `reg_file_1`。
2. `comb_awake()` 从 `inst_r` 中筛选 Load 且未被杀伤条目输出唤醒。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制 PRF 当前状态到 `_1` 工作副本。
- **输入依赖**：`reg_file[]`、`inst_r[]`。
- **输出更新**：`reg_file_1[]`、`inst_r_1[]`。
- **约束/优先级**：仅镜像，不进行读/写/旁路决策。

### 4.2 `comb_req_ftq`
- **功能描述**：在指令发射阶段向 FTQ 提取 PC 读请求。
- **输入依赖**：`iss2prf->iss_entry[]`。
- **输出更新**：`ftq_prf_pc_req->req[]`。
- **约束/优先级**：只有需要 PC 或者分支预测信息的指令才会发起请求。

### 4.3 `comb_read`
- **功能描述**：为本拍发射条目读取源操作数、应用旁路，并集成 FTQ 查表结果。
- **输入依赖**：`iss2prf->iss_entry[]`、`reg_file[]`、`inst_r[]`、`exe2prf->bypass[]`、`ftq_prf_pc_resp->resp[]`。
- **输出更新**：`prf2exe->iss_entry[]`（含 `src1_rdata/src2_rdata` 以及 `pc`、`ftq_pred_taken`、`ftq_next_pc` 等信息）。
- **约束/优先级**：`src_en=0` 时读值为 0；执行级旁路优先级高于写回级与寄存器堆。

### 4.4 `comb_awake`
- **功能描述**：从写回寄存器筛选 Load 结果并生成唤醒端口。
- **输入依赖**：`inst_r[]`、`dec_bcast->{mispred,br_mask}`。
- **输出更新**：`prf_awake->wake[]`。
- **约束/优先级**：仅 Load 且未被 squash 条目参与；最多输出 `LSU_LOAD_WB_WIDTH` 路。

### 4.5 `comb_complete`
- **功能描述**：保留阶段接口（当前无额外逻辑）。
- **输入依赖**：无。
- **输出更新**：无。
- **约束/优先级**：用于保持模块阶段调用结构一致。

### 4.6 `comb_write`
- **功能描述**：将写回级结果写入 `reg_file_1`。
- **输入依赖**：`inst_r[]` 的 `valid/dest_en/dest_preg/result`。
- **输出更新**：`reg_file_1[]`。
- **约束/优先级**：`x0` 恒为 0，不允许被写回覆盖。

### 4.7 `comb_pipeline`
- **功能描述**：推进 `inst_r` 流水并执行 flush/mispred/clear_mask 处理。
- **输入依赖**：`rob_bcast->flush`、`exe2prf->entry[]`、`dec_bcast->{mispred,br_mask,clear_mask}`。
- **输出更新**：`inst_r_1[]`（valid/uop/br_mask）。
- **约束/优先级**：flush 最高优先；mispred 命中条目无效化；存活条目需清除 `clear_mask`。

---

## 5. 存储器类型与端口

> 说明：`*_1` 是 next-state 工作副本（组合阶段写、`seq` 提交），不代表额外硬件端口；端口统计按模块行为语义给出。

### 5.1 物理寄存器堆（`reg_file`）
类型：寄存器堆（SRAM 语义）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `PRF_NUM` | `2 * ISSUE_WIDTH` | `ISSUE_WIDTH` |

端口分配说明：
- 读口：`comb_read` 为每个 issue 槽位读取 `src1/src2`。
- 写口：`comb_write` 从 `inst_r` 写回 `dest_preg`。
- `x0` 语义：`preg=0` 强制保持 0，不参与有效写回。

### 5.2 写回流水寄存器（`inst_r`）
类型：寄存器堆（按 issue 端口保存上一拍回写条目）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ISSUE_WIDTH` | `ISSUE_WIDTH` | `ISSUE_WIDTH` |

端口分配说明：
- 读口：`comb_write` 和 `comb_awake` 读取 `inst_r` 生成写回与唤醒。
- 写口：`comb_pipeline` 从 `exe2prf->entry` 采样并做 flush/mispred/clear 过滤后写入。
