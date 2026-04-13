# Dis (Dispatch) 设计文档

## 1. 概述 (Overview)
`Dispatch` 位于 `Rename` 与 `Issue/ROB/LSU` 之间，负责：

1. 为指令分配 ROB/LDQ/STQ 相关索引与标志。
2. 将宏指令拆分为 1~N 个 issue uop 并路由到目标 IQ。
3. 结合 ROB/IQ/LSU 资源与串行化约束，生成最终 `dis_fire`。
4. 维护 Dispatch 级 busy_table，并在背压时保留流水寄存器。

---

## 2. 接口定义 (Interface Definition)

### 2.1 输入接口 (`DisIn`)

| 信号/字段 | 位宽 | 来源 | 描述 |
| :--- | :--- | :--- | :--- |
| `ren2dis->uop[i]` | `RenDisInst` | Rename | 待分派指令 |
| `ren2dis->valid[i]` | 1 | Rename | 槽位有效 |
| `rob2dis->ready/empty/enq_idx/rob_flag` | - | ROB | ROB 可用性与分配基址 |
| `iss2dis->ready_num[iq]` | `IQ_NUM` 路 | ISU | 各 IQ 剩余可接收数 |
| `lsu2dis->{stq_tail,stq_tail_flag,stq_free,ldq_free,ldq_alloc_*}` | - | LSU | LSU 队列分配信息 |
| `prf_awake` / `iss_awake` | - | PRF/ISU | 唤醒总线 |
| `rob_bcast->flush` | 1 | ROB | 全局冲刷 |
| `dec_bcast->{mispred,clear_mask}` | - | IDU | 分支恢复广播 |

### 2.2 输出接口 (`DisOut`)

| 信号/字段 | 位宽 | 去向 | 描述 |
| :--- | :--- | :--- | :--- |
| `dis2ren->ready` | 1 | Rename | 上游反压 |
| `dis2rob->valid/uop/dis_fire` | `DECODE_WIDTH` | ROB | ROB 入队与发射确认 |
| `dis2iss->req[iq][slot]` | `IQ_NUM * dispatch_width` | ISU | 分派到各 IQ 的请求 |
| `dis2lsu->alloc_req/ldq_alloc_req/...` | LSU 端口宽度 | LSU | STQ/LDQ 分配与元数据 |

---

## 3. 微架构设计 (Microarchitecture)

### 3.1 两阶段分派

1. `comb_dispatch`：先做“尝试性”IQ 分配并缓存 `dispatch_cache`。
2. `comb_fire`：统一做最终发射判定（含 ROB/串行化/older block），并对失败槽位回滚 IQ 请求。

### 3.2 指令拆分 (`decompose_inst`)

1. 常规算术/分支/Load：1 uop。
2. Store：拆为 `STA + STD`。
3. `JAL/JALR`：拆为 `ADD(PC+4) + JUMP`。
4. AMO：按 LR/SC/RMW 拆为 1~3 uop。

### 3.3 Busy 与唤醒

1. `comb_wake` 根据 `busy_table + wakeup` 修正 `src_busy`。
2. `comb_fire` 在最终 `dis_fire` 成功后将目的 preg 置 busy。

---

## 4. 组合逻辑功能描述 (Combinational Logic)

### 4.1 `comb_begin`
- **功能描述**：复制流水寄存器与 busy_table 到 `_1` 工作副本。
- **输入依赖**：`inst_r/inst_valid`, `busy_table`。
- **输出更新**：`inst_r_1/inst_valid_1`, `busy_table_1`。
- **约束/优先级**：纯镜像，不做资源决策。

### 4.2 `comb_alloc`
- **功能描述**：预分配 ROB/LDQ/STQ 元数据，并生成 `dis2rob` 初始输出。
- **输入依赖**：`inst_r/inst_valid`, `in.rob2dis`, `in.lsu2dis`, `in.dec_bcast->clear_mask`。
- **输出更新**：`inst_alloc[]`, `out.dis2rob->valid/uop`, `stq_port_owner[]`, `ldq_port_owner[]`, `out.dis2lsu` 初值。
- **约束/优先级**：资源不足时对应槽位 `dis2rob->valid=0`；入队前先执行 `br_mask &= ~clear_mask`。

### 4.3 `comb_wake`
- **功能描述**：计算每条候选指令的源操作数 busy 状态。
- **输入依赖**：`busy_table`, `in.prf_awake`, `in.iss_awake`, `inst_alloc`, `out.dis2rob->valid`。
- **输出更新**：`inst_alloc[].src1_busy/src2_busy`。
- **约束/优先级**：同拍更早槽位若写同一 preg，后续槽位对应源保持 busy。

### 4.4 `comb_dispatch`
- **功能描述**：拆分指令并尝试写入 IQ 请求，记录 `dispatch_cache/dispatch_success_flags`。
- **输入依赖**：`inst_valid`, `inst_alloc`, `in.iss2dis->ready_num`, `GLOBAL_IQ_CONFIG`, `decompose_inst()`。
- **输出更新**：`out.dis2iss->req`, `dispatch_cache`, `dispatch_success_flags`, `out.dis2rob->uop[i].expect_mask/cplt_mask`。
- **约束/优先级**：按槽位顺序分配；遇不满足容量的槽位后停止后续分配。

### 4.5 `comb_fire`
- **功能描述**：统一决定最终 `dis_fire` 与 `dis2ren->ready`，并确认/回滚 IQ 与 LSU 请求。
- **输入依赖**：`out.dis2rob->valid`, `dispatch_success_flags`, `dispatch_cache`, `inst_valid/inst_r/inst_alloc`, `in.rob2dis`, `in.rob_bcast`, `in.dec_bcast`, `in.prf_awake`, `in.iss_awake`。
- **输出更新**：`out.dis2rob->dis_fire`, `out.dis2ren->ready`, `out.dis2iss->req`（回滚后）, `out.dis2lsu` fire 结果, `busy_table_1`。
- **约束/优先级**：
1. `flush/mispred/stall` 阻断发射。
2. CSR/AMO 受串行化约束（需 ROB 空且无更早指令同拍发射）。
3. older 槽位阻塞会传递到后续槽位。

### 4.6 `comb_pipeline`
- **功能描述**：推进 Dispatch 流水寄存器，并在背压时保留未发射条目。
- **输入依赖**：`in.ren2dis`, `out.dis2ren->ready`, `out.dis2rob->dis_fire`, `inst_valid/inst_r`, `inst_alloc`, `in.rob_bcast->flush`, `in.dec_bcast->{mispred,clear_mask}`。
- **输出更新**：`inst_valid_1[]`, `inst_r_1[]`。
- **约束/优先级**：flush/mispred 时全清；保留项 busy 状态由 `inst_alloc` 与唤醒结果修正。

---

## 5. 性能计数器 (Performance Counters)

| 计数器名称 | 含义 | 描述 |
| :--- | :--- | :--- |
| `dis2ren_not_ready_*` | Dispatch 反压分类统计 | 记录 ROB/串行化/IQ/LDQ/STQ 等阻塞原因 |
| `stall_rob_full_cycles` | ROB 满导致停顿周期 | 发射受 ROB 容量限制 |
| `stall_iq_full_cycles` | IQ 满导致停顿周期 | 发射受 IQ 容量限制 |
| `stall_ldq_full_cycles` | LDQ 满导致停顿周期 | Load 分配失败 |
| `stall_stq_full_cycles` | STQ 满导致停顿周期 | Store 分配失败 |

---

## 6. 存储器类型与端口

> 说明：文中“寄存器堆”是逻辑抽象，物理实现可为 FF 阵列或 SRAM（视容量与端口需求而定）。

### 6.1 Dispatch 流水寄存器（`inst_r` / `inst_valid`）
类型：流水寄存器阵列（按槽位保存待分派指令）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `DECODE_WIDTH` | `1`（按槽位读取） | `1`（按槽位写回） |

端口分配说明：
- 读口：`comb_alloc/comb_wake/comb_dispatch/comb_fire` 读取当前槽位状态。
- 写口：`comb_pipeline` 在 `ready` 时采样 `ren2dis`，背压时保留未 fire 槽位，`seq` 提交。

### 6.2 Busy Table（`busy_table`）
类型：位图状态阵列（preg busy 状态）

| 深度 | 读端口 | 写端口 |
| :--- | :--- | :--- |
| `PRF_NUM` | `2 * DECODE_WIDTH` | `DECODE_WIDTH + LSU_LOAD_WB_WIDTH + MAX_WAKEUP_PORTS` |

端口分配说明：
- 读口：`comb_wake` 对每槽位 `src1/src2` 查询 busy 位。
- 写口 A：`dis_fire && dest_en` 时将 `dest_preg` 置 busy。
- 写口 B：接收 `prf_awake` 与 `iss_awake` 时对对应 `preg` 清 busy。
