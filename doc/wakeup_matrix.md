# Wakeup Matrix 机制原理解析

## 1. 概述
Wakeup Matrix（唤醒矩阵）是一种高效的指令唤醒机制，用于替代传统的基于 CAM（Content Addressable Memory，内容寻址存储器）的唤醒逻辑。在乱序执行处理器中，指令必须等到其源操作数（Source Operands）就绪后才能被发射（Issue）到执行单元。

本项目通过引入 Wakeup Matrix，将发射队列（Issue Queue, IQ）的唤醒复杂度从 $O(N)$ 降低到了 $O(1)$（或 $O(W)$，取决于唤醒宽度），显著提升了仿真效率，并更贴近高性能处理器的硬件实现。

## 2. 传统 CAM 方式 vs. 唤醒矩阵

### 2.1 CAM 方式 (旧机制)
- **原理**：每个 IQ 槽位（Slot）都包含比较逻辑。当功能单元广播结果（Tag/Physical Register Index）时，**所有**有效的 IQ 槽位都会监听总线，并将广播的 Tag 与自己的源操作数 Tag 进行比较。
- **软件模拟**：遍历整个 IQ 数组（例如 64 个条目），逐一检查 `src_preg == broadcast_preg`。
- **硬件缺点**：
    - **功耗高**：每次唤醒都需要全局广播和所有槽位的比较器翻转。
    - **时序瓶颈**：随着 IQ 深度增加，广播线路负载变大，限制了频率。
- **复杂度**：$O(IQ\_SIZE \times WAKEUP\_WIDTH)$。

### 2.2 Wakeup Matrix 方式 (新机制)
- **原理**：维护一个全局的矩阵（或表格），记录“哪些指令正在等待某个物理寄存器”。
    - **行索引**：物理寄存器号 (Physical Register Index)。
    - **内容**：位掩码 (Bitmask)，第 $i$ 位为 1 表示 IQ 中的第 $i$ 号槽位正在等待该寄存器。
- **软件模拟**：直接通过物理寄存器号查表，获取等待该寄存器的指令列表（位掩码），然后直接操作这些指令。
- **硬件优点**：
    - **低功耗**：通过索引直接读取 RAM/寄存器堆的一行，无需全局比较。
    - **高频率**：读取操作简单，且不随 IQ 负载变化。
- **复杂度**：$O(WAKEUP\_WIDTH)$。对于每一条广播的指令，直接定位到需要唤醒的消费者。

## 3. 实现细节

### 3.1 数据结构
在 `IssueQueue` 类中维护两个向量，分别对应源操作数 1 和源操作数 2：
```cpp
// 索引：物理寄存器号 (PRF_NUM)
// 值：位掩码，表示 IQ 中等待该寄存器的 Slot集合
std::vector<uint64_t> wake_matrix_src1;
std::vector<uint64_t> wake_matrix_src2;
```

### 3.2 工作流程

#### A. 入队 (Dispatch/Enqueue)
当指令进入 IQ 分配到槽位 `idx` 时：
1. 检查 `src1` 是否有效且未就绪 (busy)。若是，设置 `wake_matrix_src1[uop.src1_preg]` 的第 `idx` 位。
2. 检查 `src2` 是否有效且未就绪 (busy)。若是，设置 `wake_matrix_src2[uop.src2_preg]` 的第 `idx` 位。

代码示例：
```cpp
if (uop.src1_en && uop.src1_busy) {
    wake_matrix_src1[uop.src1_preg] |= (1ULL << i);
}
```

#### B. 唤醒 (Wakeup)
当执行单元完成计算，广播结果寄存器 `preg` 时：
1. **查表**：读取 `mask = wake_matrix_src1[preg]`。
2. **清除**：`wake_matrix_src1[preg] = 0`。
   - *硬件语义*：一次唤醒信号发出后，等待该数据的依赖链即被满足，矩阵对应行应复位。
3. **设置就绪**：遍历 `mask` 中所有为 1 的位（Slot `idx`），将对应的指令标记为就绪。
   - *鲁棒性检查*：代码中增加了 `entry[idx].uop.src1_preg == preg` 的检查，防止因 Slot 复用（比如 Flush 后旧指令被新指令覆盖）导致的错误唤醒。

#### C. 刷新 (Flush)
当流水线刷新（如分支预测失败）导致 IQ 中的指令被移除时：
- **惰性清除 (Lazy Clearing)**：可以在 `flush` 时不操作矩阵。
- **安全性**：
    - 旧的矩阵位会在 `Enqueue` 新指令时被覆盖（如果新指令也等待同一个寄存器）。
    - 或者在 `Wakeup` 时通过鲁棒性检查被忽略（如果新指令等待不同的寄存器）。
- **完全清除**：在 `flush_all`（全流水线刷新）时，为了状态整洁，我们显式清空了整个矩阵。

## 4. 总结
Wakeup Matrix 通过“空间换时间”（消耗微小的存储空间，约 1.25KB）实现了 $O(1)$ 的唤醒查找。这在软件仿真中减少了无效遍历，在硬件设计中则是降低功耗、提升主频的关键技术。
