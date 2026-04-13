# Isu (Issue Unit) 设计文档

## 1. 概述 (Overview)
`Isu` 是后端调度单元，负责在 `Dispatch` 与 `Exu` 之间完成：

1. 按 IQ 类型接收并缓存待发射 uop。
2. 依据源操作数就绪状态执行唤醒与调度。
3. 在端口/FU 可用时发射到 `iss2prf`。
4. 维护多周期指令的延迟唤醒管线。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `dis2iss->req[iq][w]` | Dispatch | 各 IQ 入队请求 |
| `exe2iss->ready[port]` | Exu | 发射端口是否可接收 |
| `exe2iss->fu_ready_mask[port]` | Exu | 端口对应 FU 能力可用掩码 |
| `prf_awake->wake[]` | PRF/LSU | 慢速唤醒源 |
| `rob_bcast->flush` | ROB | 全局冲刷 |
| `dec_bcast->{mispred,br_mask,clear_mask}` | IDU/EXU链路 | 分支恢复与已解析分支清理 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `iss2dis->ready_num[iq]` | Dispatch | 每个 IQ 的剩余容量 |
| `iss2prf->iss_entry[port]` | PRF/Exu | 发射到执行级的 uop |
| `iss_awake->wake[]` | Rename/Dispatch | ISU 汇总后的唤醒广播 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 IQ 组织
`Isu` 在 `init()` 中根据 `GLOBAL_IQ_CONFIG` 与 `GLOBAL_ISSUE_PORT_CONFIG` 动态构建 IQ：

1. 每个 IQ 绑定 `size/dispatch_width/supported_ops`。
2. 通过 `port_start_idx + port_num` 认领物理端口，建立 IQ->端口映射。
3. 端口 id 使用 `GLOBAL_ISSUE_PORT_CONFIG` 的数组下标，避免跨翻译单元 `__COUNTER__` 差异。

### 3.2 唤醒来源

1. 慢速唤醒：`prf_awake`（Load/回写）。
2. 延迟唤醒：`latency_pipe` 倒计时归零。
3. 快速唤醒：本拍发射且单周期完成的目的寄存器。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制 IQ 与延迟管线状态到 `_1` 工作副本。
- **输入依赖**：`iqs[].entry/count`, `latency_pipe`。
- **输出更新**：`iqs[].entry_1/count_1`, `latency_pipe_1`。
- **约束/优先级**：仅镜像，不改变调度决策。

### 4.2 `comb_ready`
- **功能描述**：计算各 IQ 可接收条目数。
- **输入依赖**：`iqs[i].size/count`。
- **输出更新**：`out.iss2dis->ready_num[i]`。
- **约束/优先级**：纯容量通告。

### 4.3 `comb_enq`
- **功能描述**：将 `dis2iss` 请求入队对应 IQ。
- **输入依赖**：`in.dis2iss->req`, `configs[i].dispatch_width`, 当前 IQ 状态, `out.iss_awake`（入队前唤醒叠加）。
- **输出更新**：IQ 内部条目（通过 `enqueue` 写 `entry_1/count_1`）。
- **约束/优先级**：入队失败触发 Assert；仅 valid 请求入队。

### 4.4 `comb_issue`
- **功能描述**：从各 IQ 选择可发射条目并驱动 `iss2prf`。
- **输入依赖**：`q.schedule()` 结果, `in.exe2iss->ready`, `in.exe2iss->fu_ready_mask`, `in.rob_bcast->flush`, `in.dec_bcast->mispred`。
- **输出更新**：`out.iss2prf->iss_entry[]`, IQ `commit_issue` 状态。
- **约束/优先级**：需同时满足端口 ready 与 FU mask；flush/mispred 时不发射。

### 4.5 `comb_calc_latency_next`
- **功能描述**：构建下一拍延迟唤醒列表。
- **输入依赖**：`latency_pipe`, `out.iss2prf->iss_entry`, `get_latency(op)`。
- **输出更新**：`latency_pipe_1`。
- **约束/优先级**：仅 `latency>1 && dest_en` 进入延迟管线。

### 4.6 `comb_awake`
- **功能描述**：汇总三类唤醒源并执行 IQ 唤醒与外部广播。
- **输入依赖**：`in.prf_awake`, `latency_pipe`, `out.iss2prf->iss_entry`, `get_latency(op)`, `iqs[]`。
- **输出更新**：`iqs[].wakeup(...)`, `out.iss_awake->wake[]`。
- **约束/优先级**：唤醒端口数不超过 `MAX_WAKEUP_PORTS`。

### 4.7 `comb_flush`
- **功能描述**：处理 flush/mispred 清空，并清除已解析分支 bit。
- **输入依赖**：`in.rob_bcast->flush`, `in.dec_bcast->{mispred,br_mask,clear_mask}`, `latency_pipe_1`, `iqs[]`。
- **输出更新**：IQ 内容与 `latency_pipe(_1)`。
- **约束/优先级**：flush 高于 mispred；clear_mask 作用于存活条目。

---

## 5. IQ 建模说明（特殊）
当前 `IssueQueue` 子模块在代码上更偏“软件容器 + 调度算法”风格，而不是严格 RTL 级 SRAM/CAM 分解：

1. 以 `entry/count` 与 `schedule()/commit_issue()` 抽象行为。
2. 端口与能力通过配置动态绑定，而非固定硬编码网表。
3. 文档中建议将其视作“周期准确行为模型”，而非门级实现描述。

---

## 6. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `slots_core_bound_iq` | IQ/调度相关阻塞 | 上游 Dispatch 因 IQ 不可接收产生的后端阻塞 |
| `issue_rate` | 发射效率 | 每周期有效发射 uop 数量 |

---

## 7. 存储器类型与端口


### 7.1 IQ 条目阵列（`iqs[i].entry`）
类型：多组寄存器堆（每个 IQ 一组）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `sum_i GLOBAL_IQ_CONFIG[i].size` | `sum_i GLOBAL_IQ_CONFIG[i].size`（调度全表读） | `sum_i (GLOBAL_IQ_CONFIG[i].dispatch_width + GLOBAL_IQ_CONFIG[i].port_num)`（入队+出队） |

端口分配说明：
- 写口 A：`comb_enq` 每个 IQ 每拍最多写 `dispatch_width` 个新条目。
- 写口 B：`comb_issue` 每个 IQ 每拍最多提交 `port_num` 个已发射条目（置 invalid）。
- 读口：`schedule()` 读取条目做 ready/年龄/端口匹配。

### 7.2 IQ Wakeup Matrix（`wake_matrix_src1/src2`）
类型：位图寄存器堆（依赖反向索引）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `2 * PRF_NUM * ceil(GLOBAL_IQ_CONFIG[i].size/64)`（每个 IQ） | `MAX_WAKEUP_PORTS`（每个 IQ） | `GLOBAL_IQ_CONFIG[i].dispatch_width + GLOBAL_IQ_CONFIG[i].port_num + MAX_WAKEUP_PORTS`（每个 IQ） |

端口分配说明：
- 写口 A：入队时 `set_dep_bits_for_slot` 设置源依赖位。
- 写口 B：发射出队时 `clear_dep_bits_for_slot` 清除依赖位。
- 读/写口 C：`wakeup()` 按 preg 读取对应 bitmask，并在处理后清零该行词位。

### 7.3 延迟唤醒管线（`latency_pipe`）
类型：理论上是移位寄存器

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| 动态（由在飞多周期指令数决定） | 全表遍历读 | 全表重建写 |

端口分配说明：
- `comb_calc_latency_next` 读取旧表并重建 `latency_pipe_1`（倒计时递减 + 新发射多周期条目入队）。
- `comb_flush` 在 `flush/mispred` 下执行清空或按 `br_mask` 删除。
