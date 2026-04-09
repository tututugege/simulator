# 后端架构总体设计
## 1. 概述
后端实现了基于 **RV32IMABSU** 指令集的乱序执行流水线。其核心设计目标是支持时钟精确 (Cycle-Accurate) 的微架构仿真，能够正确运行 Linux 操作系统，并提供详细的 TMA (Top-Down Microarchitecture Analysis) 性能分析。

后端逻辑从指令译码 (Decode) 开始，到指令提交 (Commit) 结束。

## 2. 核心组件
后端由以下核心功能模块组成，各模块通过 `IO.h` 定义的标准接口进行交互：

- **Pre (PreIduQueue，包含 FTQ 与 Inst Buffer)**：后端与前端预测逻辑的桥梁，负责接收解耦的前端指令块与分支预测元数据，维护 FTQ/IBUF，并向 Idu 提供待译码条目。它是后端指令流的起点。
- **Idu (译码)**：后端的译码阶段。负责将取指阶段获得的原始指令流转换为对应的信息，并分配分支 mask，处理Mispred。
- **Rename (寄存器重命名)**：通过 RAT (Register Alias Table) 将架构寄存器映射到物理寄存器，消除 WAW 和 WAR 相关。
- **Dispatch (分派)**：将重命名后的指令分派至发射队列 (Issue Queue)、重排序缓存 (ROB) 以及在访存单元 (LSU) 里提前申请位置。
- **Isu (发射队列)**：实现乱序执行的核心。负责维护就绪状态并根据功能单元可用性选择指令发射执行。
- **Prf (物理寄存器堆)**：存储所有推测执行的结果，并提供数据转发 (Bypass) 路径。
- **Exu (执行单元)**：包含 ALU (算术运算)、Mul/Div (乘除法)、BRU (分支验证) 和 CSR (系统指令) 等多种功能单元。
- **Lsu (访存单元)**：管理所有内存访问，包含 Load Queue 和 Store Queue，确保访存顺序及数据一致性（该部分后端文档不详细描述）。
- **Rob (重排序缓存)**：负责维护指令的程序序。在指令确认无误后按序提交，并处理异常与误预测冲刷。
- **Csr (Control and Status Registers)**：管理处理器特权级状态、中断与异常。

---

`Isu`采用了两层 class 的结构，`Exu` 为了让不同的 `Fu` 拥有统一的接口使用了 `AbstractFU` 抽象类和虚函数，`Lsu`不在本文档的范围内，其余模块均是比较干净的，高度硬件化描述的。

## 3. 前后端交互接口
由于本项目前端与后端由不同风格的代码实现，交互点主要通过 `BackTop` 模块定义的 `Back_in` 和 `Back_out` 进行封装。

### 3.1 前端 -> 后端 (`Back_in`)
后端通过此接口接收取指结果及预测信息。

| 信号/字段 | 位宽 (bits) | 方向 | 描述 |
| :--- | :--- | :--- | :--- |
| `inst` | `FETCH_WIDTH * 32` | 输入 | 原始指令机器码 |
| `pc` | `FETCH_WIDTH * 32` | 输入 | 对应的程序计数器地址 |
| `valid` | `FETCH_WIDTH * 1` | 输入 | 指令有效位 |
| `predict_dir` | `FETCH_WIDTH * 1` | 输入 | 前端 BPU 的跳转方向预测 |
| `predict_next_fetch_address` | `FETCH_WIDTH * 32` | 输入 | 预测的下一取指块地址 |
| `tage_idx` / `tag` | 动态定义 | 输入 | TAGE 预测器的索引与标签元数据 (TN_MAX=4) |
| `sc_used/pred/sum/idx` | 动态定义 | 输入 | TAGE Statistical Correlator (SC) 级联预测元数据 |
| `loop_used/hit/pred/idx/tag` | 动态定义 | 输入 | 循环预测器 (Loop Predictor) 元数据 |
| `page_fault_inst` | `FETCH_WIDTH * 1` | 输入 | 前端取指触发的页面错误标志 |
| `alt_pred` | `FETCH_WIDTH * 1` | 输入 | BPU 备选路径预测位 |
| `altpcpn` / `pcpn` | `FETCH_WIDTH * 8` | 输入 | 分支预测相关的路径索引元数据 |

### 3.2 后端 -> 前端 (`Back_out`)
后端通过此反馈信号控制前端的取指行为及同步系统状态。

| 信号/字段 | 位宽 (bits) | 方向 | 描述 |
| :--- | :--- | :--- | :--- |
| `stall` | `1` | 输出 | 后端资源满（如 ROB/IQ 满），触发前端暂停 |
| `fire` | `FETCH_WIDTH * 1` | 输出 | 握手确认信号，指示后端已接收对应指令 |
| `flush` | `1` | 输出 | 流水线冲刷信号（如发生异常、误预测恢复等） |
| `mispred` | `1` | 输出 | 分支预测失败指示位 |
| `redirect_pc` | `32` | 输出 | 误预测修正或异常处理后的重定向目标 PC |
| `fence_i` | `1` | 输出 | 指令缓存一致性请求 |
| `itlb_flush` | `1` | 输出 | 指令 TLB 冲刷请求 |
| `commit_entry` | `COMMIT_WIDTH` 组 | 输出 | 流水线确认提交的指令信息，主要用于向前端和 BPU 提供延迟真实的预测训练 (Training) 样本反馈 |
| `sstatus` / `mstatus` | `32` | 输出 | 处理器系统状态寄存器 |
| `satp` | `32` | 输出 | 虚拟地址转换控制寄存器 |
| `privilege` | `2` | 输出 | 当前处理器特权级 (U/S/M) |

---

## 4. 数据流与控制流
后端采用 **分布式控制 + 集中式提交** 的策略：
1. **顺序阶段**：Decode、Rename、Dispatch 是严格按程序序执行的顺序级。
2. **乱序执行**：一旦指令进入 Issue Queue 并满足操作数就绪条件，即可乱序发射至 Exu。执行结果通过旁路 (Bypass) 快速传递给后续指令。
3. **顺序提交**：所有指令在执行完成后，必须在 ROB 中等待成为队头指令，经确认无异常后方可更新架构状态 (RAT & Memory)。

---

## 5. 设计风格说明
后端整体采用模块化 C++ 类定义，强调：
- **时钟级建模**：严格区分组合逻辑 (`comb`) 和时序逻辑 (`seq`)。
- **接口显式化**：所有模块间的信号交互必须通过 `IO` 结构体进行，避免隐式全局变量，以确保持久对标的一致性。

---

## 6. 组合逻辑依赖链
`BackTop::comb()` 的顺序就是同周期信号传播顺序。若把 `comb_begin` 也计入，并采用“尽量前推（ASAP）”的严格拓扑分层（硬依赖不变，同时保留同模块内部先后约束），则当前实现（`back-end/BackTop.cpp`）可整理为 **15 层流程**（`L0~L14`）：

| 层级 | 调用 (`comb_xxx`) | 依赖上一层的谁（关键上游） |
| :--- | :--- | :--- |
| L0 | `pre->comb_begin` | 上一拍 `PreIduQueue` 时序态。 |
|  | `idu->comb_begin` | 上一拍 `Idu` 时序态。 |
|  | `rename->comb_begin` | 上一拍 `Ren` 时序态。 |
|  | `dis->comb_begin` | 上一拍 `Dispatch` 时序态。 |
|  | `isu->comb_begin` | 上一拍 `Isu` 时序态。 |
|  | `prf->comb_begin` | 上一拍 `Prf` 时序态。 |
|  | `exu->comb_begin` | 上一拍 `Exu` 时序态。 |
|  | `rob->comb_begin` | 上一拍 `Rob` 时序态。 |
|  | `csr->comb_begin` | 上一拍 `Csr` 时序态。 |
| L1 | `pre->comb_accept_front` | `L0` 后的 `front2pre` 与队列状态。 |
|  | `idu->comb_decode` | `L0` 后 `pre->out.issue`。 |
|  | `csr->comb_interrupt` | `L0` 后 CSR 状态。 |
|  | `rename->comb_alloc` | `L0` 后 freelist/rename 寄存态。 |
|  | `prf->comb_complete` | `L0` 后 PRF 状态（占位接口）。 |
|  | `prf->comb_awake` | `L0` 后写回流水态（`inst_r`）+ 调用点可见的 `dec_bcast`（不依赖本拍稍后 `idu->comb_branch` 的新广播）。 |
|  | `prf->comb_write` | `L0` 后 PRF 写回流水态。 |
|  | `isu->comb_ready` | `L0` 后各 IQ 计数。 |
|  | `lsu->comb_lsu2dis_info` | `L0` 后 LSU 队列状态。 |
|  | `idu->comb_branch` | `L0` 后 `br_latch`。 |
|  | `rob->comb_ready` | `L0` 后 ROB 队头 + `lsu2rob` 元信息。 |
|  | `rob->comb_ftq_pc_req` | `L0` 后 ROB 队头项。 |
|  | `exu->comb_ftq_pc_req` | `L0` 后 EXU 在飞项。 |
|  | `lsu->comb_load_res` | `L0` 后 LSU 内部状态 + `dcache2lsu` 响应。 |
|  | `exu->comb_to_csr` | `L0` 后 EXU 执行槽状态。 |
| L2 | `pre->comb_ftq_lookup` | `L1` 的 `rob/exu ftq_pc_req`。 |
|  | `dis->comb_alloc` | `L1` 的 `ren2dis/rob2dis/lsu2dis/dec_bcast`。 |
|  | `csr->comb_csr_read` | `L1` 的 `exu->comb_to_csr`（`exe2csr.re`）。 |
|  | `rename->comb_rename` | `L1` 的 `alloc_reg/inst_r`。 |
| L3 | `rob->comb_commit` | `L2` 的 `ftq_lookup` 响应 + `dec_bcast`。 |
| L4 | `exu->comb_exec` | `L3` 的 `rob_bcast/dec_bcast` + `L2` 的 `csr2exe`。 |
|  | `csr->comb_exception` | `L3` 的 `rob_bcast/rob2csr`。 |
| L5 | `rob->comb_complete` | `L4` 的 `exu2rob`。 |
|  | `exu->comb_ready` | `L4` 的 `issue_stall` 与 `rob_bcast/dec_bcast`。 |
|  | `csr->comb_csr_write` | `L4` 的 trap/ret 判定 + `L1` 的 `exe2csr`。 |
| L6 | `isu->comb_issue` | `L5` 的 `exe2iss.ready/mask` + `L0` 的各 IQ 状态（`schedule`）+ `L3/L1` 的 `rob_bcast.flush` / `dec_bcast.mispred`。 |
| L7 | `lsu->comb_recv` | `L6` 的 `exe2lsu`。 |
|  | `isu->comb_awake` | `L6` 的 `iss2prf` + `L1` 的 `prf_awake` + `L0` 的 `latency_pipe`。 |
|  | `isu->comb_calc_latency_next` | `L6` 的 `iss2prf` + `L0` 的 `latency_pipe`。 |
|  | `prf->comb_read` | `L6` 的 `iss2prf` + `L0` 的 `reg_file/inst_r` + `L4` 的 `exe2prf.bypass`。 |
| L8 | `dis->comb_wake` | `L2` 的 `dis_alloc` + `L7` 的 `iss_awake` + `L1` 的 `prf_awake`。 |
| L9 | `dis->comb_dispatch` | `L8` 的 `dis_wake` + `L2` 的 `dis_alloc` + `L1` 的 `iss2dis.ready_num`。 |
| L10 | `dis->comb_fire` | `L9` 的 `dispatch_cache/dispatch_success_flags` + `L1` 的 `rob2dis/dec_bcast/prf_awake` + `L3` 的 `rob_bcast` + `L7` 的 `iss_awake`。 |
| L11 | `rename->comb_fire` | `L10` 的 `dis2ren.ready` + `L1/L2` 的 `ren2dis.valid/uop` + `L3` 的 `rob_commit/rob_bcast/dec_bcast`。 |
|  | `isu->comb_enq` | `L10` 裁剪后的 `dis2iss.req` + `L7` 的 `iss_awake`（入队前去 busy）+ `L0` 的 IQ 副本基线。 |
|  | `dis->comb_pipeline` | `L10` 的 `dis_fire/dis2ren.ready` + `L8` 的 wake 结果 + `L1` 的 `ren2dis/dec_bcast` + `L3` 的 `rob_bcast`。 |
| L12 | `idu->comb_fire` | `L11` 的 `ren2dec.ready` + `L1` 的 `dec2ren.valid/uop` + `L3` 的 `rob_bcast` + `L4` 的 `exu2id`（写 `br_latch_1`）+ 上一拍 `br_latch`。 |
|  | `rename->comb_pipeline` | `L11` 的 `ren2dec.ready/fire` + `L1` 的 `dec2ren` + `L3` 的 `rob_bcast` + `L1` 的 `dec_bcast`。 |
| L13 | `rob->comb_fire` | 统一处理 `flush/mispred/enqueue`（优先级 `flush > mispred > enqueue`）。 |
|  | `isu->comb_flush` | `L3` 的 `rob_bcast.flush` + `L1` 的 `dec_bcast.{mispred,br_mask,clear_mask}`，并覆盖 `L11` 后的 IQ/`L7` 的 `latency_pipe_1` 副本。 |
|  | `lsu->comb_flush` | `L13` 的 `rob_flush`。 |
|  | `pre->comb_fire` | `L12` 的 `idu_consume` + `L3` 的 `rob_commit` + `L1` 的 push 缓存（`push_entries`）+ 上一拍锁存的 `idu_br_latch` + `rob_bcast.flush`。 |
| L14 | `prf->comb_pipeline` | `L13` 的 `rob_flush` + `L1` 已广播的 `dec_bcast.{mispred,br_mask,clear_mask}` + `L4` 的 `exe2prf.entry`。 |
|  | `exu->comb_pipeline` | `L13` 后 `prf_read` 与 flush/mispred/clear_mask 最终状态。 |
