# Isu (Issue Unit) 设计文档

## 1. 概述 (Overview)
`Isu` 是后端调度单元，负责在 `Dispatch` 与 `Exu` 之间完成：

1. 按 IQ 类型接收并缓存待发射 uop。
2. 依据源操作数就绪状态执行唤醒与调度。
3. 在端口/FU 可用时发射到 `iss2prf`。
4. 维护多周期指令的延迟唤醒。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口

| 信号/字段 | 来源 | 描述 |
| :--- | :--- | :--- |
| `dis2iss->req[iq][w]` | Dispatch | 各 IQ 入队请求 |
| `exe2iss->ready[port]` | Exu | 发射端口是否可接收 |
| `exe2iss->fu_ready_mask[port]` | Exu | 端口对应 FU 能力可用掩码 |
| `prf_awake->wake[]` | PRF | 慢速唤醒源（Load 写回） |
| `rob_bcast->flush` | ROB | 全局冲刷 |
| `dec_bcast->{mispred,br_mask,clear_mask}` | IDU/EXU链路 | 分支恢复与已解析分支清理 |

### 2.2 输出接口

| 信号/字段 | 去向 | 描述 |
| :--- | :--- | :--- |
| `iss2dis->ready_num[iq]` | Dispatch | 每个 IQ 的剩余容量 |
| `iss2prf->iss_entry[port]` | PRF | 发射 uop（由 PRF 读数后送 Exu） |
| `iss_awake->wake[]` | Dispatch | ISU 汇总后的唤醒广播 |

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

### 3.3 软件配置与硬件的关系
`Isu` 代码里大量使用 `GLOBAL_IQ_CONFIG / GLOBAL_ISSUE_PORT_CONFIG` 做“查表”，这是**模拟器实现手段**，不是硬件运行时行为。应按下面方式理解：

1. **IQ 连接哪些发射端口、端口支持哪些 FU**  
   在硬件里是固定连线，不是周期级可变配置。  
   代码中的 `init()` 查表只是在软件里一次性构建这组固定拓扑。

2. **`q.schedule()` 里的端口选择**  
   在硬件里等价为“已知连线集合上的仲裁/选择逻辑”，不是访问可编程配置 RAM。

3. **`supported_ops` / `capability_mask`**  
   在硬件里可视作译码后进入组合门控网络的常量掩码；  
   在软件里以字段和查表表达，便于维护与参数化。

4. **是否存在‘硬件查表’**  
   当前模型中没有“运行时查配置表再决定连线”的硬件语义。  
   这些表仅用于代码组织，语义上等价于 RTL 中写死的参数与连线。

### 3.4 唤醒矩阵原理（Wakeup Matrix）
`IssueQueue` 内部维护两组位矩阵：`wake_matrix_src1` 与 `wake_matrix_src2`，本质是按物理寄存器号索引的反向依赖表。

1. 行索引：`preg`（目的物理寄存器号）。
2. 行内 bit：对应 IQ 槽位 `slot` 是否在该源操作数上等待这个 `preg`。
3. 入队时：若某条目 `src*_busy=1`，就在对应矩阵位置置位。
4. 唤醒时：收到 `preg` 后直接读取该行 bitmask，批量定位要清 busy 的槽位，再把该行清零。

相较于 CAM 的实现，Wakeup Matrix 应会更省面积，并且时序压力小。

### 3.5 发射队列三大电路：分配 / 选择 / 唤醒
从硬件语义看，IQ 可拆成三块组合逻辑：

1. **分配（Allocate/Enqueue）**  
   对应 `comb_enq`：把 Dispatch 给出的请求写入空槽，并建立依赖（busy 位与 wakeup matrix 标记）。

2. **选择（Select/Issue Select）**  
   对应 `comb_issue`：在“已就绪条目 + 可用端口/FU 能力”约束下进行仲裁，产生本拍 `iss2prf`。

3. **唤醒（Wakeup）**  
   对应 `comb_awake`：汇总慢速/延迟/快速唤醒源，清理 IQ 内依赖 busy，并向外广播 `iss_awake`。

代码中这三步是顺序调用；硬件视角是同拍内的组合传播链，在拍边界由时序单元统一提交。

### 3.6 为什么“选择”和“唤醒”要同一个周期
选择与唤醒同拍的核心目的是降低 issue-bubble，提升吞吐：

1. **减少额外 1 拍等待**  
   若把唤醒延后一拍，本拍刚产出的结果无法让依赖者在下一拍立即参与选择，会平白损失吞吐。

2. **形成最短反馈环**  
   本拍已发射的单周期指令可通过快速唤醒在同拍更新 IQ 就绪态，使下一拍选择看到最新 ready 信息。

3. **与实际 OoO 核心一致**  
   真实实现通常将 wakeup-select 设计为紧耦合路径（同周期组合），必要时通过分簇/分级仲裁控制时序压力。

4. **本实现中的时序含义**  
   `comb_issue` 先决定“本拍谁发射”；`comb_awake` 再基于发射结果与回写源更新就绪。  
   两者都在同一拍完成，`seq()` 统一提交，使下一拍调度看到“已唤醒”的 IQ 状态。

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
当前 `IssueQueue` 子模块在代码上更偏“软件容器 + 调度算法”风格，而不是严格 RTL 级 SRAM/CAM 分解；但其行为语义与固定硬件结构一一对应：

1. 以 `entry/count` 与 `schedule()/commit_issue()` 抽象行为。
2. 端口与能力在代码中通过配置绑定，但语义等价于硬件静态连线。
3. 实际的硬件`IQ`主要就是需要做好分配、选择、唤醒三部分，分配比较简单，就是01串寻找1，选择可以简单也可以复杂，目前我们还是用最简单的01串找1，也就是根据IQ idx确定优先级，唤醒的话基于唤醒矩阵的唤醒电路也比较简单。所以暂时认为IQ的硬件实现应该不复杂。
4. 实际上不同的`IQ`需要的信息不同，例如`LD_IQ`是不需要`src2`的信息的，不过为了代码方便，我们保留了一部分冗余。
5. `latency pipe`实际上是为乘除法器设计的移位寄存器，需要根据其延迟配置移位寄存器的深度，不过目前的理解更像是一个表，每项有一个计数器。
---

## 6. 性能计数器 (Performance Counters)

当前 `Isu` 代码中未新增模块内专属计数器字段；相关统计复用全局性能统计路径。

---

## 7. 资源占用 (Resource Usage)

| 名称 | 规格 | 描述 |
| :--- | :--- | :--- |
| IQ 条目数组 | `sum(GLOBAL_IQ_CONFIG[i].size)` | 各 IQ 存储待发射条目 |
| 延迟唤醒管线 | 动态向量 | 跟踪多周期目的寄存器倒计时 |
| 唤醒广播口 | `MAX_WAKEUP_PORTS` | 对外发布本拍唤醒结果 |
