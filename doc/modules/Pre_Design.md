# Pre (PreIduQueue) 设计文档

## 1. 概述 (Overview)
`PreIduQueue` 位于前端与 `Idu` 之间，承担后端入口解耦与取指路径元数据管理（可能和前端最后的front2back_FIFO重合，简单看作InstBuffer容量扩充）。
核心职责：

1. 接收 `FrontPreIO` 指令块并在可接收时写入 `InstructionBuffer`（IBUF）。
2. 为每个接收块分配 `FTQ` 条目，保存预测相关元数据，供 EXU/ROB 查询 PC。
3. 向 `Idu` 输出 `PreIssueIO`（`entries[]`）。
4. 在 `flush`/`mispred` 下执行 IBUF 清空与 FTQ flush/recover。
5. 根据 `rob_commit` 回收已退休的 FTQ 条目。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口 (`PreIduQueueIn`)

| 信号名 | 位宽 | 方向 | 来源 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `front2pre->inst[i]` | `FETCH_WIDTH * 32` | 输入 | Front-end | 取指指令字 |
| `front2pre->pc[i]` | `FETCH_WIDTH * 32` | 输入 | Front-end | 对应 PC |
| `front2pre->valid[i]` | `FETCH_WIDTH * 1` | 输入 | Front-end | 输入槽位有效位 |
| `front2pre->predict_* / tage_* / sc_* / loop_*` | 参数化数组 | 输入 | Front-end/BPU | 分支预测与训练元数据 |
| `front2pre->page_fault_inst[i]` | `FETCH_WIDTH * 1` | 输入 | Front-end | 前端 Inst Page Fault 标记 |
| `idu_consume->fire[i]` | `DECODE_WIDTH * 1` | 输入 | Idu | Idu 本拍实际消费槽位 |
| `rob_bcast->flush` | `1` | 输入 | Rob | 全局冲刷 |
| `rob_commit->commit_entry[i]` | `COMMIT_WIDTH * InstEntry` | 输入 | Rob | 提交信息（用于 FTQ 回收） |
| `idu_br_latch->mispred` | `1` | 输入 | Idu (`br_latch`) | 已锁存分支误预测标志 |
| `idu_br_latch->ftq_idx` | `FTQ_IDX_WIDTH` | 输入 | Idu (`br_latch`) | 误预测分支对应 FTQ 索引 |
| `ftq_exu_pc_req->req[i]` | `FTQ_EXU_PC_PORT_NUM * FtqPcReadReq` | 输入 | Exu | EXU 侧 FTQ 读请求 |
| `ftq_rob_pc_req->req[i]` | `FTQ_ROB_PC_PORT_NUM * FtqPcReadReq` | 输入 | Rob | ROB 侧 FTQ 读请求 |

### 2.2 输出接口 (`PreIduQueueOut`)

| 信号名 | 位宽 | 方向 | 去向 | 描述 |
| :--- | :--- | :--- | :--- | :--- |
| `pre2front->ready` | `1` | 输出 | Front-end | 后端是否可接收新 fetch group |
| `pre2front->fire[i]` | `FETCH_WIDTH * 1` | 输出 | Front-end | 本拍实际接收的输入槽位 |
| `issue->entries[i]` | `DECODE_WIDTH * InstructionBufferEntry` | 输出 | Idu | 给 Idu 的候选指令条目 |
| `ftq_exu_pc_resp->resp[i]` | `FTQ_EXU_PC_PORT_NUM * FtqPcReadResp` | 输出 | Exu | EXU 侧 FTQ 读响应 |
| `ftq_rob_pc_resp->resp[i]` | `FTQ_ROB_PC_PORT_NUM * FtqPcReadResp` | 输出 | Rob | ROB 侧 FTQ 读响应 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 状态组织

1. `ibuf/ibuf_1`：指令缓冲（当前态/下一拍工作副本）。
2. `ftq_entries/ftq_entries_1`：FTQ 条目数组（当前态/下一拍工作副本）。
3. `ftq_head/tail/count` 与 `_1`：FTQ ring 指针与占用。

### 3.2 主工作流

1. `comb_begin`：镜像状态并给 `issue/pre2front` 输出默认值。
2. `comb_accept_front`：进行 backpressure 判定；若接收成功，分配 FTQ 并缓存待 push 的 IBUF 条目。
3. `comb_ftq_lookup`：响应 EXU/ROB 的 FTQ 读请求。
4. `comb_fire`：统一处理 consume/pop、flush/recover、IBUF 更新与 commit reclaim。
5. `seq`：只做 `*_1 -> 当前态` 提交。

### 3.3 关键优先级

1. `rob_bcast->flush` 优先级高于 `mispred recover`。
2. `flush/mispred` 时 IBUF 直接清空，不做 pop/push 正常路径。
3. `flush/recover` 生效拍不做 FTQ commit reclaim。

### 3.4 场景矩阵

| 场景 | 输入特征 | 模块行为 |
| :--- | :--- | :--- |
| 正常 steady-state | 无 `flush/mispred`，front 有效 | 接收 front，分配 FTQ，IBUF pop+push 并行进行 |
| backpressure | `ibuf` 满或 `ftq` 满 | `pre2front->ready=0`，不接收新 block |
| 全局 flush | `rob_bcast->flush=1` | FTQ 清空，IBUF 清空，阻断前端接收 |
| 分支恢复 | `idu_br_latch->mispred=1` | FTQ tail recover 到 `ftq_idx+1`，IBUF 清空 |
| page fault 指令 | `front2pre->page_fault_inst[i]=1` | fault 标志随 `InstructionBufferEntry` 透传到 Idu |

### 3.5 逐拍示例（normal）

1. 第 N 拍：`comb_accept_front` 接收 1 个 fetch block，写 `ftq_entries_1[tail]`，生成 `push_entries[]`。
2. 第 N 拍：`comb_fire` 根据 `idu_consume->fire[]` 弹出 IBUF 头部，并把 `push_entries[]` 追加到 `ibuf_1`。
3. 第 N->N+1 跳变：`seq` 提交 `ibuf_1/ftq_*_1`，第 N+1 拍 `comb_begin` 可见新状态。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- 功能描述：镜像状态并初始化 `issue/pre2front` 默认输出。
- 输入依赖：`ibuf`、`ftq_head/tail/count`、`ftq_entries`。
- 输出更新：`ibuf_1`、`ftq_*_1`、`ftq_entries_1`、`out.issue`、`out.pre2front`、`push_count`。
- 约束/优先级：只做镜像和默认驱动，不做接收/恢复决策。

### 4.2 `comb_accept_front`
- 功能描述：判断是否接收前端输入并准备 FTQ/IBUF 写入。
- 输入依赖：`in.front2pre`、`in.rob_bcast->flush`、`in.idu_br_latch->mispred`、`ibuf`、`ftq_count`。
- 输出更新：`out.pre2front->ready/fire`、`ftq_entries_1`、`ftq_tail_1`、`ftq_count_1`、`push_entries[]`。
- 约束/优先级：`flush/mispred` 禁止接收；同拍最多分配一个 FTQ entry。

### 4.3 `comb_ftq_lookup`
- 功能描述：处理 EXU/ROB 的 FTQ 读请求并返回响应。
- 输入依赖：`in.ftq_exu_pc_req`、`in.ftq_rob_pc_req`、`ftq_entries`。
- 输出更新：`out.ftq_exu_pc_resp`、`out.ftq_rob_pc_resp`。
- 约束/优先级：仅 `req.valid=1` 的端口返回有效数据，其余端口输出默认空响应。

### 4.4 `comb_fire`
- 功能描述：统一执行 consume、flush/recover、IBUF 更新和 FTQ 提交回收。
- 输入依赖：`in.idu_consume`、`in.rob_bcast`、`in.idu_br_latch`、`in.rob_commit`、`push_entries[]`。
- 输出更新：`ibuf_1`、`ftq_head_1`、`ftq_tail_1`、`ftq_count_1`、`ftq_entries_1`。
- 约束/优先级：`flush > recover > normal`；flush/recover 时跳过 commit reclaim。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 触发位置 |
| :--- | :--- | :--- |
| `ib_consume_available_slots` | IBUF 可供 IDU 消费的槽位累计 | `comb_begin` |
| `ib_blocked_cycles` | 因 IBUF 容量不足导致 front 被阻塞 | `comb_accept_front` |
| `ftq_blocked_cycles` | 因 FTQ 满导致 front 被阻塞 | `comb_accept_front` |
| `ib_write_cycle_total` | IBUF 写入发生周期数 | `comb_accept_front` |
| `ib_write_inst_total` | IBUF 写入指令总数 | `comb_accept_front` |
| `ib_consume_consumed_slots` | IBUF 被 IDU 消费槽位累计 | `comb_fire` |

---

## 6. 资源占用 (Resource Usage)

| 名称 | 规格 | 描述 |
| :--- | :--- | :--- |
| `ibuf/ibuf_1` | `INSTRUCTION_BUFFER_SIZE` | 指令缓冲当前态与工作副本 |
| `ftq_entries/ftq_entries_1` | `FTQ_SIZE` | FTQ 元数据表当前态与工作副本 |
| `ftq_head/tail/count` + `_1` | ring 指针与计数 | FTQ 管理状态 |

---

## 7. 已知限制与后续优化 (Known Limitations / Future Work)

1. `push_entries/push_count` 目前为文件级 `static`，单实例模型成立，多实例并行仿真不安全。
2. 并入前端的`front2back_FIFO`

---

## 8. 变更记录 (Change Log)

| 日期 | 变更摘要 |
| :--- | :--- |
| 2026-04-08 | 新增 `Pre_Design.md`，与当前 `comb_begin/comb_accept_front/comb_ftq_lookup/comb_fire` 实现对齐。 |
| 2026-04-08 |  `Pre` 不再维护本地 `br_latch`。 |
| 2026-04-08 |  整理了接口，将 `PreIduQueue` 正式作为后端的硬件组成 |
