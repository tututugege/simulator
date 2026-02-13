# 后端架构总体设计 (Backend Architecture Overview)

## 1. 概述
本引擎后端实现了基于 **RV32IMASU** 指令集的乱序执行流水线。其核心设计目标是支持时钟精确 (Cycle-Accurate) 的微架构仿真，能够正确运行 Linux 操作系统，并提供详细的 TMA (Top-Down Microarchitecture Analysis) 性能分析。

后端逻辑从指令译码 (Decode) 开始，到指令提交 (Commit) 结束。

## 2. 核心组件
后端由以下核心功能模块组成，各模块通过 `IO.h` 定义的标准接口进行交互：

- **FTQ (Fetch Target Queue)**：后端与前端预测逻辑的桥梁，存储取指路径信息供后端 BRU 最终验证。它是后端指令流的逻辑起点。
- **Idu (Decode/译码)**：后端的入口。负责将取指阶段获得的原始指令流转换为微指令 (MicroOps)，并提取分支预测元数据。
- **Rename (寄存器重命名)**：通过 RAT (Register Alias Table) 将架构寄存器映射到物理寄存器，消除 WAW 和 WAR 相关。
- **Dispatch (分派)**：将重命名后的指令分派至发射队列 (Issue Queue)、重排序缓存 (ROB) 以及访存单元 (LSU)。
- **Isu (Issue/发射队列)**：实现乱序执行的核心。负责维护就绪状态并根据功能单元可用性选择指令发射执行。
- **Prf (Physical Register File/物理寄存器堆)**：存储所有推测执行的结果，并提供数据转发 (Bypass) 路径。
- **Exu (Execute/执行单元)**：包含 ALU (算术运算)、Mul/Div (乘除法)、BRU (分支验证) 和 CSR (系统指令) 等多种功能单元。
- **Lsu (Load Store Unit/访存单元)**：管理所有内存访问，包含 Load Queue 和 Store Queue，确保访存顺序及数据一致性。
- **Rob (Reorder Buffer/重排序缓存)**：负责维护指令的程序序。在指令确认无误后按序提交，并处理异常与误预测冲刷。
- **Csr (Control and Status Registers)**：管理处理器特权级状态、中断与异常。

---

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
| `tage_idx` | `FETCH_WIDTH * 4 * 32` | 输入 | TAGE 预测器的索引元数据 (TN_MAX=4) |
| `page_fault_inst` | `FETCH_WIDTH * 1` | 输入 | 前端取指触发的页面错误标志 |
| `alt_pred` | `FETCH_WIDTH * 1` | 输入 | BPU 备选路径预测位 |
| `altpcpn` / `pcpn` | `FETCH_WIDTH * 8` | 输入 | 分支预测相关的路径索引元数据 |

### 3.2 后端 -> 前端 (`Back_out`)
后端通过此反馈信号控制前端的取指行为及同步系统状态。

| 信号/字段 | 位宽 (bits) | 方向 | 描述 |
| :--- | :--- | :--- | :--- |
| `stall` | `1` | 输出 | 后端资源满（如 ROB/IQ 满），触发前端暂停 |
| `fire` | `FETCH_WIDTH * 1` | 输出 | 握手确认信号，指示后端已接收对应指令 |
| `flush` | `1` | 输出 | 流水线冲刷信号（如发生异常、系统状态变更等） |
| `mispred` | `1` | 输出 | 分支预测失败指示位 |
| `redirect_pc` | `32` | 输出 | 误预测修正或异常处理后的重定向目标 PC |
| `fence_i` | `1` | 输出 | 指令缓存一致性请求 |
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

## 6. 组合逻辑依赖链 (Combinational Dependency Chain)
由于本项目追求 IO 精确，`BackTop::comb()` 中的模块调用顺序决定了同一周期内信号的传播路径。下表详细列出了执行顺序及其关键输入依赖。

| 顺序 | 模块 | 函数 (`comb_xxx`) | 功能简述 | 关键输入依赖 (上游信号) |
| :--- | :--- | :--- | :--- | :--- |
| 组别 (Group) | 模块 | 函数 (`comb_xxx`) | 功能简述 | 关键输入依赖 |
| :--- | :--- | :--- | :--- | :--- |
| **Group 1: Status & Commit**<br>*(检查状态，提交指令)* | Csr | `comb_csr_status` | 更新特权级/SATP | 上一周期寄存器状态 |
| | Rob | `comb_commit` | 指令提交 | `rob_head` 状态 (seq更新) |
| | Csr | `comb_write` | CSR 写操作 | `rob_commit` (Group 1) |
| | Lsu | `comb_commit` | Store 提交 | `rob_commit` (Group 1) |
| | Prf | `comb_br_check` | 检查误预测 | 执行阶段结果 (WB Latch) |
| | Isu | `comb_release_tag` | 释放分支Tag | `rob_commit` (Group 1) |
| **Group 2: Exception & Recovery**<br>*(生成冲刷信号，回滚状态)* | Csr | `comb_interrupt` | 中断检测 | `mip`, `mie`, `mstatus`, `privilege` |
| | Csr | `comb_exception` | 异常汇总 | `rob_commit`, `interrupt` (Group 2) |
| | Rob | `comb_flush` | 全局冲刷信号 | `exception`, `mispred` (Group 1/2) |
| | Rob | `comb_branch` | 分支重定向 | `mispred` (Group 1) |
| | Idu | `comb_branch` | 前端重定向 | `mispred`, `redirect_pc` |
| | Lsu/Isu/Dis | `comb_flush` | 模块级冲刷 | `rob_bcast.flush` (Group 2) |
| **Group 3: Execution & Writeback**<br>*(执行计算，产生结果与唤醒)* | Exu | `comb_exec` | ALU/Mul/Div计算 | `isu2exe` (Issue Latch) |
| | Lsu | `comb_load_res` | Load 结果回写 | Memory response |
| | Prf | `comb_pipeline` | 锁存执行结果 | `exe2prf` (Group 3) |
| | Prf | `comb_complete` | 标记完成 | `exe2prf` (Group 3) |
| | Rob | `comb_complete` | 更新ROB计数 | `prf2rob` (Group 3) |
| | Prf | `comb_awake` | 生成唤醒信号 | `exe2prf`, `wb_bypass` (Group 3) |
| **Group 4: Issue & Read**<br>*(被唤醒，读取操作数)* | Isu | `comb_wakeup` | 唤醒 IQ 指令 | `prf_awake` (Group 3) |
| | Isu | `comb_select` | 选择发射指令 | Ready Bit Mask |
| | Isu | `comb_issue` | 发射指令 | Selected Mask |
| | Prf | `comb_read` | 读取物理寄存器 | `iss2prf` (Group 4), Bypass Network |
| | Exu | `comb_to_csr` | CSR 预处理 | `iss2exe` |
| | Csr | `comb_csr_read` | CSR 读操作 | `exe2csr` (Group 4) |
| **Group 5: Dispatch & Alloc**<br>*(资源分配，流水线推进)* | Rob | `comb_ready` | ROB 满/空检查 | `enq_ptr`, `deq_ptr` |
| | Isu | `comb_ready` | IQ 满/空检查 | `count` |
| | Lsu | `comb_lsu2dis_info`| STQ/LDQ 空间检查 | `stq_count` |
| | Ren | `comb_alloc` | 分配物理寄存器 | `free_vec` |
| | Ren | `comb_rename` | 源操作数重命名 | `spec_RAT`, `dis2ren` |
| | Dis | `comb_alloc` | 汇总资源状态 | `rob/isu/lsu/ren` ready 信号 |
| | Dis | `comb_dispatch` | 分派指令 | All Ready |
| | Dis | `comb_fire` | 确认分派 | `fire` 信号 |
| | Rob/Ren/Lsu | `comb_fire`/`alloc` | 更新指针与映射表 | `dis_fire` (Group 5) |


> [!IMPORTANT]
> 组合逻辑链条的顺序至关重要。例如，`Ren::comb_wake` (Step 30) 必须发生在 `Ren::comb_rename` (Step 32) 之前，以确保处于发射阶段 (Issue Stage) 刚刚唤醒的指令能够通过旁路 (Bypass) 直接被当前正在重命名的依赖指令捕获（实现 0 周期唤醒）。
