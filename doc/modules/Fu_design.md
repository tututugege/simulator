# Functional Unit (FU) 接口协议与接入规范

本文档旨在说明模拟器后端执行单元（Functional Unit, FU）的标准接口协议。无论是单周期计算单元（如 ALU）、长延迟流水线单元（如 MUL），还是变长迭代单元（如 DIV），或者是通过外部接入的真实的 RTL 级黑盒 IP，只要严格遵守本协议进行包装，即可无缝接入模拟器的发射与写回体系，实现完全解耦。

## 1. 接口定义

所有 FU 都必须继承 `AbstractFU` 或具有相同的硬件接口定义：

```cpp
struct FuInput {
  wire<1> en = 0;                     // Issue 端发起的请求有效信号
  wire<1> consume = 0;                // 写回端发起的接收确认信号
  wire<1> flush = 0;                  // 全局冲刷信号
  wire<BR_MASK_WIDTH> flush_mask = 0; // 分支预测失败的冲刷掩码
  wire<BR_MASK_WIDTH> clear_mask = 0; // 分支预测成功的清除掩码
  ExuInst inst;                       // 进来的指令负载包
};

struct FuOutput {
  wire<1> ready = 0;                  // FU 向 Issue 端表明自己是否空闲/可接收
  wire<1> complete = 0;               // FU 向写回端表明计算已完成，结果有效
  ExuInst inst;                       // 出去的指令负载包（含计算结果）
};
```

`ExuInst` 字段（`PrfExeUop` + Exu 扩展）建议按代码视图对照（位宽同源码宏）：

```cpp
struct ExuInst : public PrfExeIO::PrfExeUop {
  // from PrfExeUop
  wire<32> pc;
  wire<1> ftq_resp_valid;
  wire<1> ftq_pred_taken;
  wire<32> ftq_next_pc;
  wire<PRF_IDX_WIDTH> dest_preg;
  wire<PRF_IDX_WIDTH> src1_preg;
  wire<PRF_IDX_WIDTH> src2_preg;
  wire<32> src1_rdata;
  wire<32> src2_rdata;
  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> is_atomic;
  wire<1> dest_en;
  wire<1> src1_en;
  wire<1> src2_en;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm;
  wire<BR_TAG_WIDTH> br_id;
  wire<BR_MASK_WIDTH> br_mask;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<1> stq_flag;
  wire<LDQ_IDX_WIDTH> ldq_idx;
  wire<1> rob_flag;
  wire<UOP_TYPE_WIDTH> op;
  DebugMeta dbg;

  // Exu extra
  wire<32> diag_val;
  wire<32> result;
  wire<1> mispred;
  wire<1> br_taken;
  wire<1> page_fault_inst;
  wire<1> flush_pipe;
  wire<1> ftq_entry_valid;
};
```

对于黑盒 RTL：  
- 若端口存在同名/同语义信号，直接连通。  
- 若某字段当前硬件不需要，可悬空（在包装层给默认值 0）。  

## 2. 握手协议 (Handshaking Protocol)

整个 FU 的数据流类似于两级标准的 `Valid-Ready` 握手协议。

### 2.1 接收指令阶段 (Issue -> FU)

- **`out.ready` (FU -> Issue)**
  - **语义**：当前周期 FU 内部是否有空余槽位接收新的指令。
  - **规则**：如果 FU 内部的输入缓存未满，或者它能在本周期马上处理完一条指令腾出空间，就应该拉高 `ready`。
  - **注意**：发射队列 (Issue Queue) 强依赖 `ready` 信号，只有在 `ready == 1` 时，才会对该端口发射指令。

- **`in.en` (Issue -> FU) & `in.inst`**
  - **语义**：当前周期 Issue Queue 向 FU 发射了一条有效指令。
  - **规则**：当且仅当 `out.ready == 1` 且存在可发射的指令时，Issue 会拉高 `in.en`。FU 应当在此时通过组合逻辑获取 `in.inst`（如果是单周期），或在时钟沿将 `in.inst` 锁存到内部。

> **握手成功条件**：当本周期 `out.ready == 1` 且 `in.en == 1` 时，说明一条指令成功打入 FU。

### 2.2 写回结果阶段 (FU -> Writeback)

- **`out.complete` (FU -> Writeback) & `out.inst`**
  - **语义**：FU 内部的计算已经结束，结果已经放置在 `out.inst` 中准备好被写回。
  - **规则**：当指令计算完毕后拉高 `complete`，并且在没有收到 `consume` 确认前，**必须保持 `complete == 1` 和 `out.inst` 数据稳定不变**。

- **`in.consume` (Writeback -> FU)**
  - **语义**：写回网络 (Writeback Bus) 或后续流水线在本周期成功接收了 FU 吐出的结果。
  - **规则**：当 FU 看到本周期 `out.complete == 1` 且 `in.consume == 1` 时，说明输出握手成功。FU 必须在下一个时钟周期将该指令出队，或将状态机转回 Idle（如果之前被堵住的话，同时拉高 `ready`）。

> **握手成功条件**：当本周期 `out.complete == 1` 且 `in.consume == 1` 时，指令成功离开 FU。

## 3. 异常与恢复机制

在乱序执行的 CPU 中，由于分支预测错误或异常，流水线中的指令需要被及时冲刷。

### 3.1 全局冲刷 (Global Flush)

- **`in.flush`**
  - **触发**：发生了异常、中断等必须清空整个流水线后端的事件。
  - **FU 行为**：一旦看到 `in.flush == 1`，FU 应当**立刻无条件清空**内部所有的状态、队列、流水段以及正在执行的计算过程。回到初始的空闲状态，并在下一周期恢复 `out.ready = 1`，`out.complete = 0`。

### 3.2 分支预测冲刷 (Branch Misprediction Flush)

- **`in.flush_mask`**
  - **触发**：某一条分支指令被判定为预测错误，后端下发了属于该错误路径的分支 Mask。
  - **FU 行为**：FU 必须检查内部每一条正在执行/排队的指令。如果某条指令的 `inst.uop.br_mask & in.flush_mask != 0`，说明该指令是错误预测路径上的废指令，FU 需要将其丢弃（Valid 置 0）。如果是单操作数长延迟单元（如除法器）且无法中途打断，可以选择忽略它，但完成后绝不能拉高 `complete` 或必须通知外部该结果作废（但最好能直接复位底层 RTL）。

### 3.3 分支预测清除 (Branch Resolution Clear)

- **`in.clear_mask`**
  - **触发**：某一条分支指令预测正确，其对应的依赖 Mask 可以被释放。
  - **FU 行为**：FU 需要遍历内部有效指令，执行 `inst.uop.br_mask &= ~(in.clear_mask)`。更新指令携带的分支依赖信息。

## 4. 接入真实 RTL 级 IP 的指南

如果你有一个真实的 RTL 级除法器模块（Verilog/SystemVerilog），且对外是类似 `valid-ready` 接口的黑盒，可以这样映射接入：

1. **输入包装**：
   - RTL 接口的 `i_valid` 连向 `in.en`。
   - RTL 接口的 `o_ready` 连向 `out.ready`。
   - RTL 接口的 `i_data` (操作数) 从 `in.inst.src1/src2` 提取后连入。

2. **输出包装**：
   - RTL 接口的 `o_valid` 连向 `out.complete`。
   - RTL 接口的 `i_ready` 连向 `in.consume`。
   - RTL 接口的 `o_result` 连向 `out.inst.res`，并补充其它元信息（如 `dest_preg`）

3. **冲刷机制**：
   - 将 `in.flush` 连向 RTL 模块的 `i_flush` 或直接作为一个高优先级的异步复位/同步清除信号。
   - 如果 RTL 黑盒不支持针对特定指令的分支 mask `flush_mask` 中途取消计算，加一个单独的 FIFO记录各种  mask，可以等它算完，但不在 `complete` 时通知外面（偷偷丢弃废弃的计算结果）。
