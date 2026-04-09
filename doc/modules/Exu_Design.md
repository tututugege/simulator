# Exu (Execution Unit) 设计文档

## 1. 概述
`Exu` 负责执行来自 ISU/PRF 的 uop，并输出：

1. 端口反压（`exe2iss`）
2. 旁路与写回（`exe2prf`）
3. 完成通报（`exu2rob`）
4. LSU/CSR 请求（`exe2lsu`、`exe2csr`）
5. 分支恢复广播（`exu2id`）

---

## 2. 接口定义
### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `prf2exe->iss_entry[port]` | PRF | 已读取操作数的执行输入 |
| `lsu2exe->{wb_req,sta_wb_req}` | LSU | LSU 回写/完成回调 |
| `ftq_pc_resp` | FTQ | 分支/PC 相关查询返回 |
| `csr2exe` | CSR | CSR 读返回 |
| `rob_bcast->flush` | ROB | 全局冲刷 |
| `dec_bcast->{mispred,br_mask,clear_mask}` | IDU/EXU链路 | 分支恢复与分支位清理 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `exe2iss->ready[port]` | ISU | 端口是否可接收新指令 |
| `exe2iss->fu_ready_mask[port]` | ISU | 端口上可接收的 FU 能力掩码 |
| `exe2prf->bypass[]` | PRF | 各 FU 完成结果旁路广播 |
| `exe2prf->entry[port]` | PRF | 最终写回条目 |
| `exu2rob->entry[port]` | ROB | 执行完成通报 |
| `exe2lsu->{agu_req,sdu_req}` | LSU | 地址/数据请求 |
| `exe2csr` | CSR | CSR 读写请求 |
| `exu2id->{mispred,clear_mask,redirect_*}` | IDU | 分支恢复信息 |
| `ftq_pc_req` | FTQ | ALU/BR 的 PC 查询请求 |

---

## 3. 微架构设计
### 3.1 FU 构建与端口映射
`init()` 按 `GLOBAL_ISSUE_PORT_CONFIG` 为每个物理端口构建 FU 列表：

1. `MUL/DIV/ALU/BRU/AGU/SDU/CSR/FP` 按 capability mask 挂载。
2. `port_mappings[port].entries` 保存 `<fu, support_mask>` 路由项。
3. 运行时按 `uop.op` 在该端口的固定候选集合中进行组合选择。

### 3.2 软件配置与硬件的关系
`Exu` 代码里使用 `GLOBAL_ISSUE_PORT_CONFIG` 和 `port_mappings`，这是**模拟器实现手段**，不是硬件运行时“查配置表再决定连线”。

1. **每个端口连接哪些 FU**  
   在硬件里是固定连线（编译期/综合期确定）。  
   `init()` 的查表只是在软件里一次性构建这组固定关系。

2. **`comb_exec` 的 FU 选择**  
   在硬件语义上等价于“端口已知候选 FU 上的组合仲裁/选择逻辑”，  
   不是访问可编程配置 RAM 的运行时查表行为。

3. **`support_mask` 的语义**  
   在硬件里可视作常量门控条件（译码后进入组合网络），  
   在软件里以字段方式表达，便于参数化维护。

### 3.3 三类结果路径

1. FU 完成结果 -> `bypass`（优先广播）。
2. 常规整数写回 + LSU 回调 -> `exe2prf->entry`。
3. 非 killed 完成 -> `exu2rob->entry`（含来自 LSU 的 `wb_req/sta_wb_req` 回调在存活条件下同步回传 ROB）。

### 3.4 分支恢复

1. BR 结果先汇总 `clear_mask`（所有已解析分支）。
2. 若存在 mispred，选最老误预测分支作为重定向源。
3. 广播到 `exu2id`，由 IDU 统一下发到全后端。

---

## 4. 组合逻辑功能描述
### 4.1 `comb_begin`
- **功能描述**：复制执行流水寄存器到 `_1` 工作副本。
- **输入依赖**：`inst_r[]`。
- **输出更新**：`inst_r_1[]`。
- **约束/优先级**：仅镜像，不发射不写回。

### 4.2 `comb_ftq_pc_req`
- **功能描述**：为需 PC 信息的 ALU/BR 条目生成 FTQ 读请求。
- **输入依赖**：`inst_r[]`, `rob_bcast->flush`, `dec_bcast`, 端口范围常量。
- **输出更新**：`out.ftq_pc_req->req[]`。
- **约束/优先级**：killed/flush 条目不发请求；仅匹配端口能力的条目有效。

### 4.3 `comb_ready`
- **功能描述**：生成 EXU 端口 ready 与 FU 可接收掩码。
- **输入依赖**：`inst_r[]`, `issue_stall[]`, `rob_bcast->flush`, `dec_bcast`, `port_mappings[].fu->can_accept()`。
- **输出更新**：`out.exe2iss->ready[]`, `out.exe2iss->fu_ready_mask[]`。
- **约束/优先级**：flush 时全端口 ready=0；被 kill 在飞条目不阻塞端口。

### 4.4 `comb_to_csr`
- **功能描述**：驱动 CSR 读写请求字段。
- **输入依赖**：`inst_r[0]`, `rob_bcast->flush`, CSR 指令字段。
- **输出更新**：`out.exe2csr->{we,re,idx,wcmd,wdata}`。
- **约束/优先级**：非 CSR/flush 场景输出默认无请求；仅端口 0 的 CSR 指令参与驱动。

### 4.5 `comb_pipeline`
- **功能描述**：推进执行流水并处理 flush/mispred/clear。
- **输入依赖**：`inst_r[]`, `issue_stall[]`, `prf2exe->iss_entry[]`, `rob_bcast->flush`, `dec_bcast`, `units[]`。
- **输出更新**：`inst_r_1[]`，并对 FU 执行 `flush/clear_br`。
- **约束/优先级**：flush 最高优先级；mispred 先 flush 再 clear；存活条目需清除 `clear_mask`。

### 4.6 `comb_exec`
- **功能描述**：执行路由、结果收集、写回分发、LSU 请求发送与分支仲裁。
- **输入依赖**：`inst_r[]`, `port_mappings`, `units`, `lsu2exe` 回调, `rob_bcast`, `dec_bcast`。
- **输出更新**：`issue_stall[]`, `exe2prf->{bypass,entry}`, `exu2rob->entry`, `exe2lsu->{agu_req,sdu_req}`, `exu2id->{mispred,clear_mask,redirect_*}`。
- **约束/优先级**：被 flush/kill 的结果不产生有效完成；按端口路由顺序选择 FU；分支误预测取最老条目。

---

## 5. 性能计数器
当前 `Exu` 代码中未新增模块内专属计数器字段；相关统计复用全局性能统计路径。

---

## 6. 资源占用
| 名称 | 规格 | 描述 |
| :--- | :--- | :--- |
| `inst_r`/`inst_r_1` | `ISSUE_WIDTH` | 执行级流水寄存器 |
| `units` | 与配置相关 | 各类 FU 对象实例 |

---

## 7. 附录：FU 建模说明（特殊）
`Exu` 下的各 FU 当前采用“软件对象 + 行为接口”建模；其硬件语义应理解为固定端口拓扑上的执行与仲裁：

1. 统一继承 `AbstractFU`，通过 `can_accept/accept/get_finished_uop/pop_finished/tick` 建模时序行为。
2. `port_mappings` 在软件里是容器表示；在硬件语义里对应固定连线集合，而非运行时可变路由表。
3. 文档中的 `FU` 应理解为周期准确行为模块，不等价于最终门级划分。
4. 理论上我们希望 “指令交给FU -> FU执行 -> 接收FU结果” 这三个过程是解耦合的。
