# Rob (Reorder Buffer) 设计文档

## 1. 概述 (Overview)
`Rob` 负责维护乱序执行到按序提交之间的全局顺序语义，核心职责：

1. 接收 `Dispatch` 分配的指令并按行入队。
2. 跟踪执行完成状态（含多 uop 完成掩码）。
3. 在队头执行组提交/单提交仲裁。
4. 在精确提交点触发异常、中断与全局 flush。
5. 在误预测时回退 tail 并清理错误路径条目。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `dis2rob->{uop[],dis_fire[]}` | Dispatch | 入队条目与每槽握手 |
| `exu2rob->entry[]` | Exu | 执行完成回传（完成位/分支/异常补充） |
| `lsu2rob->{tma.miss_mask,committed_store_pending}` | LSU | 头阻塞归因与 `SFENCE.VMA` 提交约束 |
| `dec_bcast->{mispred,redirect_rob_idx}` | Idu/Exu 广播链路 | 分支误预测恢复信息 |
| `csr2rob->interrupt_req` | CSR | 外部中断请求 |
| `ftq_pc_resp->resp[0]` | FTQ | 单提交 flush 指令所需 PC 查询返回 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `rob2dis->{ready,stall,empty,enq_idx,rob_flag,tma.*}` | Dispatch | ROB 反压、分配位置与头阻塞元信息 |
| `rob_commit->commit_entry[]` | Rename/LSU 等提交消费者 | 本拍提交槽位 |
| `rob_bcast->{flush,exception,interrupt,...}` | 全后端广播 | 提交点触发的冲刷/异常/陷入原因 |
| `rob_bcast->{head_valid,head_rob_idx,head_incomplete_*}` | LSU/调试/TMA | 队头与首个未完成条目索引 |
| `rob2csr->{commit,interrupt_resp}` | CSR | CSR 提交确认与中断响应 |
| `ftq_pc_req->req[0]` | FTQ | 读取 ROB 队头对应 PC |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 组织方式

1. ROB 为 `entry[ROB_BANK_NUM][ROB_LINE_NUM]` 的多 bank 行结构。
2. `enq_ptr/deq_ptr + enq_flag/deq_flag` 组成 ring 状态；`ptr` 相等时由 flag 区分满空。
3. 逻辑 `rob_idx = line * ROB_BANK_NUM + bank`。

### 3.2 完成模型

1. 每条指令携带 `expect_mask/cplt_mask`，支持一条指令由多个 uop 分步完成。
2. `comb_complete()` 按 `issue port -> cplt_bit` 回填完成位，并做重复/越界断言。

### 3.3 提交模型

1. 默认路径为组提交：队头整行有效条目均完成才可整行退休。
2. 特殊场景（flush 类指令、异常、中断、进度兜底）进入单提交。
3. flush/异常/中断仅在 `comb_commit()` 的精确提交点发出。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制 ROB 当前状态到 `_1` 工作副本。
- **输入依赖**：`entry[][]`、`enq_ptr/deq_ptr`、`enq_flag/deq_flag`。
- **输出更新**：`entry_1[][]`、`enq_ptr_1/deq_ptr_1`、`enq_flag_1/deq_flag_1`。
- **约束/优先级**：仅镜像，不做提交/分配/恢复决策。

### 4.2 `comb_ready`
- **功能描述**：生成 ROB 就绪/反压与队头阻塞分类信息。
- **输入依赖**：`entry[][deq_ptr]`、`is_empty()/is_full()`、`lsu2rob->tma.miss_mask`。
- **输出更新**：`rob2dis->{stall,tma.*,empty,ready}`、`rob2csr->{commit,interrupt_resp}` 默认值。
- **约束/优先级**：按“最老未完成”归因；队头含 flush 类指令时强制 `stall`。

### 4.3 `comb_ftq_pc_req`
- **功能描述**：请求 ROB 队头最老有效指令的 FTQ PC。
- **输入依赖**：`is_empty()`、`entry[][deq_ptr].valid`、`uop.ftq_idx/ftq_offset`。
- **输出更新**：`ftq_pc_req->req[0]`。
- **约束/优先级**：ROB 为空时无请求；每拍最多发 1 路 ROB 侧请求。

### 4.4 `comb_commit`
- **功能描述**：执行组提交/单提交仲裁，生成 `rob_commit` 与 `rob_bcast`，并推进 `deq_ptr`。
- **输入依赖**：`entry[][deq_ptr]`、`dec_bcast`、`csr2rob`、`ftq_pc_resp`、`lsu2rob`。
- **输出更新**：`rob_commit->commit_entry[]`、`rob_bcast` 全套事件位、`rob2csr`、`rob2dis->{enq_idx,rob_flag}`、`entry_1` 与 `deq_ptr_1/deq_flag_1`。
- **约束/优先级**：异常/中断/flush 仅在精确提交点触发；特殊指令和中断强制单提交。

### 4.5 `comb_complete`
- **功能描述**：接收执行完成回传并更新对应 ROB 条目完成/异常/分支信息。
- **输入依赖**：`exu2rob->entry[]`（含 `rob_idx`、fault/mispred/diag 信息）。
- **输出更新**：`entry_1[bank][line].uop.{cplt_mask,diag_val,page_fault_*,mispred,br_taken,flush_pipe,dbg}`。
- **约束/优先级**：完成位不可重复置位，不可超出 `expect_mask`；`flush_pipe` 采用 OR 保持。

### 4.6 `comb_branch`
- **功能描述**：在误预测时回退 tail 并失效重定向点之后的错误路径条目。
- **输入依赖**：`dec_bcast->{mispred,redirect_rob_idx}`、`out.rob_bcast->flush`、当前 `enq_ptr/enq_flag`。
- **输出更新**：`enq_ptr_1/enq_flag_1`、`entry_1[][]` 有效位、重定向分支 `ftq_is_last`。
- **约束/优先级**：仅在 `mispred && !flush` 生效；需处理跨行回滚。

### 4.7 `comb_fire`
- **功能描述**：接收 Dispatch 入队请求，写入当前 tail 行并在有入队时推进 tail。
- **输入依赖**：`rob2dis->ready`、`dis2rob->{dis_fire[],uop[]}`、`enq_ptr/enq_flag`。
- **输出更新**：`entry_1[][enq_ptr]`、`enq_ptr_1/enq_flag_1`。
- **约束/优先级**：仅 ready 时入队；同拍任意槽位入队则推进一行。

### 4.8 `comb_flush`
- **功能描述**：全局 flush 时清空 ROB 并复位 ring 指针。
- **输入依赖**：`out.rob_bcast->flush`。
- **输出更新**：`entry_1[][].valid`、`enq_ptr_1/deq_ptr_1`、`enq_flag_1/deq_flag_1`。
- **约束/优先级**：flush 为最终覆盖状态，生效后 ROB 回到空队列初始态。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `slots_core_bound_rob` | ROB 相关阻塞 | Dispatch 因 ROB 不可接收或队头序列化导致的后端阻塞 |
| `slots_retiring` | 提交槽位利用 | 本拍成功提交的指令槽位数 |

---

## 6. 存储器类型与端口

> 说明：`*_1` 是 next-state 工作副本（组合阶段写、`seq` 提交），不代表额外硬件端口；端口统计按模块行为语义给出。

### 6.1 ROB 条目阵列（`entry[ROB_BANK_NUM][ROB_LINE_NUM]`）
类型：多 bank 环形队列（FIFO 控制 + 按 `rob_idx` 随机更新）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ROB_BANK_NUM * ROB_LINE_NUM` | `ROB_BANK_NUM + ISSUE_WIDTH` | `DECODE_WIDTH + ISSUE_WIDTH`（steady-state） |

端口分配说明：
- 读口 A（队头行）：`comb_ready/comb_commit` 读取 `deq_ptr` 行的 `ROB_BANK_NUM` 槽位。
- 读口 B（随机）：`comb_complete` 按回传 `rob_idx` 读取对应条目并更新完成状态。
- 写口 A（入队）：`comb_fire` 按 `dis_fire` 写 tail 行，最多 `DECODE_WIDTH`。
- 写口 B（完成）：`comb_complete` 写随机条目的 `cplt_mask/异常/分支` 字段，最多 `ISSUE_WIDTH`。
- 控制路径写：`comb_branch/comb_flush` 可批量失效错误路径或全清。

### 6.2 Ring 指针与标志（`enq_ptr/deq_ptr/enq_flag/deq_flag`）
类型：寄存器

| 存储 | 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- | :--- |
| `enq_ptr` | `1` | `1` | `1` |
| `deq_ptr` | `1` | `1` | `1` |
| `enq_flag` | `1` | `1` | `1` |
| `deq_flag` | `1` | `1` | `1` |

端口分配说明：
- `comb_fire` 负责 tail 推进，`comb_commit` 负责 head 推进。
- `comb_branch/comb_flush` 在恢复路径直接改写 tail/head 与 flag。

### 6.3 停滞计数（`stall_cycle`）
类型：寄存器

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `1` | `1` | `1` |

端口分配说明：
- 在 `comb_commit` 中按提交进展更新，用于死锁保护与调试触发。
