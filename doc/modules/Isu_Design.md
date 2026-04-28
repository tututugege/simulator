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
2. 延迟唤醒：
   - `MUL`：固定延迟移位寄存器 `mul_wake_pipe` 末级命中。
   - `DIV/FP`：迭代槽位 `div_wake_slots/fp_wake_slots` 的 `countdown==0`。
3. 快速唤醒：本拍发射且单周期完成的目的寄存器。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：调用各 IQ 的 `comb_begin()` 建立本拍工作副本并清零 IQ 瞬时输入，再复制延迟管线到 `_1`。
- **输入依赖**：`iqs[]`, `mul_wake_pipe/div_wake_slots/fp_wake_slots`。
- **输出更新**：IQ 内部 `_1` 状态与 `iq.out.free_slots`，`*_wake_*_1`。
- **约束/优先级**：仅镜像，不改变调度决策。

### 4.2 `comb_ready`
- **功能描述**：计算各 IQ 可接收条目数。
- **输入依赖**：`iqs[i].out.free_slots`。
- **输出更新**：`out.iss2dis->ready_num[i]`。
- **约束/优先级**：纯容量通告。

### 4.3 `comb_enq`
- **功能描述**：将 `dis2iss` 请求写入 `iq.in.enq_reqs` 并调用 `iq.comb_enq()`。
- **输入依赖**：`in.dis2iss->req`, `configs[i].dispatch_width`, `out.iss_awake`（入队前唤醒叠加）。
- **输出更新**：IQ 内部条目（经 `iq.in` 驱动写入 `_1`）。
- **约束/优先级**：入队失败触发 Assert；仅 valid 请求入队。

### 4.4 `comb_issue`
- **功能描述**：通过 `iq.in.{issue_block,port_fu_ready_mask}` 驱动各 IQ 发射，并消费 `iq.out.issue_grants` 生成 `iss2prf`。
- **输入依赖**：`in.exe2iss->fu_ready_mask`, `in.rob_bcast->flush`, `in.dec_bcast->mispred`, `iq.out.issue_grants`。
- **输出更新**：`out.iss2prf->iss_entry[]`，IQ 内部提交状态（由 `iq.comb_issue()` 完成）。
- **约束/优先级**：需满足对应端口能力的 FU mask 不为空；flush/mispred 时不发射。

### 4.5 `comb_calc_latency_next`
- **功能描述**：构建下一拍延迟唤醒列表。
- **输入依赖**：`mul_wake_pipe/div_wake_slots/fp_wake_slots`, `out.iss2prf->iss_entry`, `get_latency(op)`。
- **输出更新**：`mul_wake_pipe_1/div_wake_slots_1/fp_wake_slots_1`。
- **约束/优先级**：仅 `latency>1 && dest_en` 进入延迟管线。

### 4.6 `comb_awake`
- **功能描述**：汇总三类唤醒源后写入 `iq.in.wake_valid[] + iq.in.wake_pregs[]` 并调用 `iq.comb_wakeup()`，同时对外广播。
- **输入依赖**：`in.prf_awake`, `mul_wake_pipe/div_wake_slots/fp_wake_slots`, `out.iss2prf->iss_entry`, `get_latency(op)`, `iqs[]`。
- **输出更新**：IQ 等待项就绪位、`out.iss_awake->wake[]`。
- **约束/优先级**：唤醒端口数不超过 `MAX_WAKEUP_PORTS`。

### 4.7 `comb_flush`
- **功能描述**：通过 `iq.in.{flush_all,flush_br,flush_br_mask,clear_mask}` 驱动 `iq.comb_flush()`，并同步清理延迟管线。
- **输入依赖**：`in.rob_bcast->flush`, `in.dec_bcast->{mispred,br_mask,clear_mask}`, `*_wake_*_1`, `iqs[]`。
- **输出更新**：IQ 内容与 `mul/div/fp wake` 状态（含 `_1`）。
- **约束/优先级**：flush 高于 mispred；clear_mask 作用于存活条目。

---

## 5. IQ 建模说明（特殊）
当前 `IssueQueue` 子模块采用“内部状态 + 对外 IO”建模，`Isu` 仅通过 `iq.in/iq.out` 交互：

1. 组合路径以 `comb_begin/comb_enq/comb_wakeup/comb_issue/comb_flush` 组织，时序由 `seq()` 提交。
2. 端口与能力通过配置动态绑定，而非固定硬编码网表。
3. 仍是周期准确行为模型，不等价于门级网表分解。

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
- 读/写口 C：`comb_wakeup()` 按 preg 读取对应 bitmask，并在处理后清零该行词位。

实现风格说明：
- Wakeup Matrix 采用双态寄存器建模：`wake_matrix_src{1,2}` / `wake_matrix_src{1,2}_1`。
- `comb_begin` 先执行 `*_1 <- *`，组合阶段统一读写 `_1`，`seq()` 再提交到当前态。

### 7.3 延迟唤醒结构（`mul/div/fp`）
类型：`MUL` 为移位寄存器，`DIV/FP` 为迭代计数槽位

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `MUL`: `ISU_MUL_WAKE_DEPTH * ISU_MUL_WAKE_SLOT_NUM`；`DIV/FP`: 各自槽位数 | 全表遍历读 | 全表重建写 |

端口分配说明：
- `comb_calc_latency_next` 读取旧状态并重建 `*_1`（`DIV/FP` 倒计时递减 + `MUL` 移位 + 新发射多周期条目入队）。
- `comb_flush` 在 `flush/mispred` 下执行清空或按 `br_mask` 删除。
