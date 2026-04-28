# Exu (Execution Unit) 设计文档

## 1. 概述 (Overview)
`Exu` 负责执行来自 ISU/PRF 的 uop，并输出：

1. 端口反压（`exe2iss`）
2. 旁路与写回（`exe2prf`）
3. 完成通报（`exu2rob`）
4. LSU/CSR 请求（`exe2lsu`、`exe2csr`）
5. 分支恢复广播（`exu2id`）

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `prf2exe->iss_entry[port]` | PRF | 已读取操作数的执行输入 |
| `lsu2exe->{wb_req,sta_wb_req}` | LSU | LSU 回写/完成回调 |
| `csr2exe` | CSR | CSR 读返回 |
| `fu2exu->entry[fu]` | FU | FU 执行后统一回传（`ready/complete/inst`） |
| `rob_bcast->flush` | ROB | 全局冲刷 |
| `dec_bcast->{mispred,br_mask,clear_mask}` | IDU/EXU链路 | 分支恢复与分支位清理 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `exe2iss->fu_ready_mask[port]` | ISU | 端口上可接收的 FU 能力掩码 |
| `exe2prf->bypass[]` | PRF | 各 FU 完成结果旁路广播 |
| `exe2prf->entry[port]` | PRF | 最终写回条目 |
| `exu2rob->entry[port]` | ROB | 执行完成通报 |
| `exe2lsu->{agu_req,sdu_req}` | LSU | 地址/数据请求 |
| `exe2csr` | CSR | CSR 读写请求 |
| `exu2id->{mispred,clear_mask,redirect_*}` | IDU | 分支恢复信息 |
| `exu2fu->entry[fu]` | FU | EXU 下发执行包（`en/consume/flush/inst`） |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 FU 构建与端口映射
`init()` 按 `GLOBAL_ISSUE_PORT_CONFIG` 为每个物理端口构建 FU 列表：

1. `MUL/DIV/ALU/BRU/AGU/SDU/CSR/FP` 按 capability mask 挂载。
2. `port_mappings[port].entries` 保存 `<fu, fu_idx, support_mask, lsu_port_meta>` 路由项。
3. 运行时按 `uop.op` 在该端口路由表中选择目标 FU。

补充说明：
- FU 不直接读/写 EXU 顶层总线（LSU/CSR）；总线访问由 EXU 收口。
- ALU/BRU 需要的 PC 及预测上下文直接由 PRF 阶段随 uop 一同传入。

### 3.2 三类结果路径

1. FU 完成结果 -> `bypass`（优先广播）。
2. 常规整数写回 + LSU 回调 -> `exe2prf->entry`。
3. 非 killed 完成 -> `exu2rob->entry`。

### 3.3 分支恢复

1. BR 结果先汇总 `clear_mask`（所有已解析分支）。
2. 若存在 mispred，选最老误预测分支作为重定向源。
3. 广播到 `exu2id`，由 IDU 统一下发到全后端。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制执行流水寄存器到 `_1` 工作副本，并统一完成 EXU 输出接口与 `exu2fu/fu2exu` 快照的默认清零。
- **输入依赖**：`inst_r[]`。
- **输出更新**：`inst_r_1[]`，以及 `exe2csr/exe2prf/exu2rob/exe2lsu/exu2id` 默认值，`out.exu2fu->entry[]` 默认值；并将 `units[].out` 快照到 `in.fu2exu->entry[]`。
- **约束/优先级**：不执行发射/写回/冲刷，仅建立本拍组合初值。

### 4.2 `comb_ready`
- **功能描述**：生成 EXU 端口 FU 可接收掩码。
- **输入依赖**：`inst_r[]`, `issue_stall[]`, `rob_bcast->flush`, `dec_bcast`, `in.fu2exu->entry[].ready`。
- **输出更新**：`out.exe2iss->fu_ready_mask[]`。
- **约束/优先级**：flush 时全端口 mask=0；被 kill 在飞条目不阻塞端口。

### 4.3 `comb_to_csr`
- **功能描述**：驱动 CSR 读写请求字段。
- **输入依赖**：`inst_r[0]`, `rob_bcast->flush`, CSR 指令字段。
- **输出更新**：`out.exe2csr->{we,re,idx,wcmd,wdata}`。
- **约束/优先级**：非 CSR/flush 场景保持 `comb_begin` 设定的默认无请求值。

### 4.4 `comb_pipeline`
- **功能描述**：推进执行流水并处理 flush/mispred/clear。
- **输入依赖**：`inst_r[]`, `issue_stall[]`, `prf2exe->iss_entry[]`, `rob_bcast->flush`, `dec_bcast`, `units[]`。
- **输出更新**：`inst_r_1[]`，并通过 `out.exu2fu->entry[].{flush,flush_mask,clear_mask}` 驱动 FU 清理，再回收至 `in.fu2exu->entry[]`。
- **约束/优先级**：flush 最高优先级；mispred 先 flush 再 clear；存活条目需清除 `clear_mask`。

### 4.5 `comb_exec`
- **功能描述**：按三段式完成执行路由、FU 执行、结果收集与写回。
- **阶段划分**：
  1. `comb_exu2fu_dispatch`：收集可发射指令，写 `out.exu2fu->entry[]`。
  2. `comb_fu_exec`：FU 只消费 `out.exu2fu`，执行后回写 `in.fu2exu->entry[]`。
  3. `comb_fu2exu_collect`：从 `in.fu2exu` 收集完成结果，完成 bypass/PRF/ROB/LSU/分支仲裁。
- **输入依赖**：`inst_r[]`, `port_mappings`, `in.fu2exu`, `lsu2exe` 回调, `rob_bcast`, `dec_bcast`。
- **输出更新**：`issue_stall[]`, `out.exu2fu->entry[]`, `in.fu2exu->entry[]`, `exe2prf->{bypass,entry}`, `exu2rob->entry`, `exe2lsu->{agu_req,sdu_req}`, `exu2id->{mispred,clear_mask,redirect_*}`。
- **约束/优先级**：被 flush/kill 的结果不产生有效完成；按端口路由顺序选择 FU；分支误预测取最老条目。

---

## 5. FU 建模说明（特殊）
`Exu` 下的各 FU 当前采用“软件对象 + 行为接口”建模：

1. 统一继承 `AbstractFU`，通过公开 `in/out` IO 与 `comb_ctrl/comb_issue/comb_consume/seq` 建模时序行为。
2. `port_mappings` 动态路由更接近“行为级端口交换矩阵”，非固定 RTL 结构网表。
3. 文档中的 `FU` 应理解为周期准确行为模块，不等价于最终门级划分。
4. EXU/FU 边界采用显式 `exu2fu/fu2exu` IO，FU 不直接驱动 EXU 对外总线。

---

## 6. 存储器类型与端口


### 6.1 执行级流水寄存器（`inst_r`）
类型：流水寄存器阵列（按 issue 端口保存在飞 uop）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ISSUE_WIDTH` | `ISSUE_WIDTH` | `ISSUE_WIDTH` |

端口分配说明：
- 读口：`comb_ready/comb_exec/comb_to_csr` 读取各端口在飞条目。
- 写口：`comb_pipeline` 在端口可推进时从 `prf2exe` 采样新条目，或清空被 kill 条目。

### 6.2 FU 内部在飞状态（`units` 内部）
类型：功能单元本地存储

| 子类型 | 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- | :--- |
| `FixedLatencyFU::pipeline` | `latency`（每实例） | `1`（取完成） | `1`（issue 接收） |
| `IterativeFU::current_inst` | `1`（每实例） | `1`（取完成） | `1`（issue 覆盖） |

端口分配说明：
- EXU 通过 `out.exu2fu->entry[fu_idx]` 驱动 FU 输入；通过 `in.fu2exu->entry[fu_idx]` 回收输出。
- FU 组合路径按 `comb_ctrl`（控制）/`comb_issue`（接收）/`comb_consume`（消费）分段执行。

### 6.3 端口路由表（`port_mappings`）
类型：只读配置表（初始化后运行期不改，实际上是软件，不需要真实的硬件实现）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `ISSUE_WIDTH`（每端口一组 `FuEntry`） | `ISSUE_WIDTH` | `0`（运行期） |

端口分配说明：
- `comb_ready/comb_exec` 每拍读取路由表选择可用 FU 与支持掩码。
