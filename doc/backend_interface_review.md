# 后端接口检查记录

## 范围

本文档记录 `back-end` 中目前仍不是 `wire<>` 或寄存器风格表达的接口字段。
这里关注的是接口形式是否足够接近 RTL 描述，而不是功能正确性。

## 问题列表

### 1. 接口 payload 中仍直接使用枚举类型

这些字段本质上属于硬件控制信息，但目前还是以 C++ 枚举形式存在，而不是定宽 `wire`。

- `InstType`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L39)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L289)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L434)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L558)
- `UopType`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L702)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L885)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1010)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1107)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1172)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1633)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1740)

### 1.1 枚举类型的建议处理方式

对于 `InstType`、`UopType` 这类字段，建议采用“两层表示”：

- 模块边界接口（主要是 `IO.h`）中只保留定宽 `wire<N>`
- 软件内部语义类型（主要是 `types.h`、模块内部控制逻辑）继续保留 `enum class`
- 在接口边界使用显式 `encode/decode` 或 `set/get` helper 完成转换

这样可以同时满足两类需求：

- RTL 组查看接口时，可以直接看到明确位宽
- 软件侧编写逻辑时，仍然保留枚举的类型检查，减少误用

建议形式如下：

- `types.h` 中定义软件语义枚举，例如 `enum class UopType : uint8_t`
- `config.h` 或公共头文件中定义编码位宽，例如 `UOP_TYPE_WIDTH`
- `IO.h` 中对应字段改为 `wire<UOP_TYPE_WIDTH>`
- 提供显式转换函数，例如：
  - `encode_uop_type(UopType op) -> wire<UOP_TYPE_WIDTH>`
  - `decode_uop_type(wire<UOP_TYPE_WIDTH> bits) -> UopType`

建议不要直接把接口字段继续保留成 C++ 枚举，原因是：

- 位宽不直观，不利于 RTL 对照
- 枚举底层存储语义不是按 bit-accurate 接口定义设计的
- 后续如果调整编码、观察波形、做 packed layout，会不够清晰

建议推进顺序：

1. 先在 `IO.h` 中把 `UopType` 改成 `wire<N>`，建立一套 `encode/decode` 模式
2. 验证这种模式稳定后，再把 `InstType` 也按同样方式处理
3. `types.h` 中的 `InstInfo/MicroOp` 暂时仍保留 `enum class`，避免重构范围一次性过大

### 2. `IO.h` 中仍存在原生标量类型

这是目前最明确、最典型的不够 RTL 风格的接口字段。

- `IssDisIO.ready_num` 使用 `int`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L797)
- `ExeIssIO.fu_ready_mask` 使用 `int64_t`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1152)
- `ExuIdIO.ftq_idx` 使用 `int`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1230)
- `LsuDisIO` 整体使用 `int` / `bool`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1595)
- `DisLsuIO` 整体使用 `bool` / `uint32_t`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1690)

### 3. 接口中仍使用 STL 容器

- `LsuRobIO::tma.miss_mask` 使用 `std::bitset<ROB_NUM>`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1613)

这个字段后续更适合改成定宽硬件 mask。

### 4. 调试和软件侧元数据仍穿过大量模块边界

这些字段不完全是硬件功能字段，但它们目前仍然是接口 payload 的一部分。
当前约定是：`dbg` 保持为模拟器专属 sideband，刻意不改成 `wire/reg`，用来和真实硬件字段区分。

- `InstDebugMeta`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L40)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L290)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L435)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L559)
- `UopDebugMeta`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L703)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L886)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1011)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1173)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1634)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1741)
- `flush_pipe` 属于真实硬件控制语义，应改为 `wire<1>`
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L291)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L436)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1174)
  - [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1635)

### 5. 更外层的后端边界仍偏软件风格

- `Back_in` 和 `Back_out` 中仍大量使用 `uint32_t`、`bool`
  - [back-end/include/BackTop.h](/home/tututu/qimeng/simulator/back-end/include/BackTop.h#L19)
  - [back-end/include/BackTop.h](/home/tututu/qimeng/simulator/back-end/include/BackTop.h#L34)

### 6. FTQ 表项仍是软件结构体风格

- `FTQEntry` 使用 `uint32_t`、`bool`、`uint64_t`
  - [back-end/include/FTQ.h](/home/tututu/qimeng/simulator/back-end/include/FTQ.h#L6)

这更偏内部状态而不是严格模块 IO，但从表达形式上仍然不是硬件风格。

### 7. 通用 payload 类型本身仍是混合风格

这些虽然不是逐模块的接口结构体，但它们强烈影响了接口定义方式。

- `InstInfo.type` 和 `MicroOp.op` 仍是枚举
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L130)
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L183)
- `dbg` 元数据中仍有 `bool` 和 `int64_t`
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L68)
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L76)
  - 这部分当前视为模拟器 sideband，允许保留非 `wire/reg` 形式
- `tma` 元数据中仍有 `bool`
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L81)
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L85)
- `flush_pipe` 属于真实硬件控制语义，应改为 `wire<1>`
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L135)
  - [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L188)

## 建议的下一步

优先从 `IO.h` 中那些最纯粹的原生标量字段开始收敛，因为这部分最容易改，而且不会立刻把 `InstInfo/MicroOp` 全部推倒重来。

建议顺序：

1. `IssDisIO.ready_num`
2. `ExeIssIO.fu_ready_mask`
3. `ExuIdIO.ftq_idx`
4. `LsuDisIO`
5. `DisLsuIO`
6. `LsuRobIO::tma.miss_mask`
