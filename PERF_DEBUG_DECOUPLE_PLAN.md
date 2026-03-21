# 模块专用 Inst/Uop 重构计划

## 0. 当前状态
- 当前工作分支：`module-specific-uops`
- 已完成：
  - `Decode -> Rename` 边界已改为 `DecRenInst`
  - `Rename -> Dispatch` 边界已改为 `RenDisInst`
  - `Dispatch -> Issue` 边界已改为 `DisIssUop`
  - `Issue -> PRF -> EXU` 主执行入口已改为 `IssPrfUop` / `PrfExeUop`
  - `EXU -> PRF` 回写边界已改为 `ExePrfWbUop`
  - `EXU -> ROB` 完成边界已改为 `ExuRobUop`
  - `EXU -> LSU` 请求边界已改为 `ExeLsuReqUop`
  - `LSU -> EXU` 回写边界已改为 `LsuExeRespUop`
  - `Dispatch -> ROB` 边界已改为 `DisRobInst`
  - `ROB -> Commit` 边界已改为 `RobCommitInst`
  - backend 中无实际用途的 `get_hardware_io()` 已删除
- 当前稳定回归基线：
  - `instruction num = 3325303`
  - `cycle num = 1014161`
  - `ipc = 3.278871`

## 1. 目标
- 不再使用共享的 `InstInfo` / `MicroOp` 作为模块间通用接口负载。
- 为每个模块边界定义专用的 `Inst` / `Uop` 结构体。
- 删除接口中的冗余信号，使 backend IO 与预期 RTL 边界一致。
- 不再依赖 `ModuleIOs.h + filter()` 这种兼容映射层，而是直接在真实边界上表达接口。

## 2. 为什么要做这件事
- 当前 [IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h) 里仍有大量边界直接传完整 `InstInfo` / `MicroOp`。
- 这会导致每个模块看到的输入输出都比真实硬件边界更宽。
- [ModuleIOs.h](/home/tututu/qimeng/simulator/back-end/include/ModuleIOs.h) 虽然定义了较瘦的参考结构，但它本质上还是适配层，不是真实接口。
- 如果要给 RTL 组提供参考模型，backend 主路径本身就应该直接使用精确边界。

## 3. 设计原则
- 一个边界，一个结构体。
- 不再保留通用 `filter()` 兼容层。
- 生产端只填写该边界实际存在的字段。
- 消费端只读取该边界真实可见的字段。
- `InstInfo` / `MicroOp` 可以暂时保留为模块内部工作结构，但不再作为模块间标准接口。
- 每次只改一条边界，每步都重新编译并运行 `dhrystone.bin`。

## 4. 范围
- 范围内：
  - [IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h)
  - [ModuleIOs.h](/home/tututu/qimeng/simulator/back-end/include/ModuleIOs.h)
  - backend 各级模块中读写共享接口结构的代码
- 范围外：
  - frontend 接口清理
  - 架构行为变更
  - `dbg` / `tma` 命名调整

## 5. 目标形态

### 5.1 指令级边界
- 用专用结构替代共享 `InstInfo`，例如：
  - `DecRenInst`
  - `RenDisInst`
  - `DisRobInst`

### 5.2 微操作级边界
- 用专用结构替代共享 `MicroOp`，例如：
  - `DisIssUop`
  - `IssPrfUop`
  - `PrfExeUop`
  - `ExeRobUop`
  - `ExeLsuReqUop`
  - `LsuExeRespUop`

### 5.3 连线方式
- 不再依赖 `ModuleIOs.h::filter(...)`
- 边界结构本身允许带少量窄范围 helper，例如：
  - `from_inst_info(...)`
  - `to_inst_info()`
  - `from_micro_op(...)`
  - `to_micro_op()`
- 这些 helper 只服务单一边界，不再承担全局适配职责。

## 6. 分步计划

### Step 0：冻结基线
- 执行：
  - `make -j8`
  - `./build/simulator ./dhrystone.bin`
- 记录：
  - 是否通过
  - 指令数
  - 周期数
  - IPC

完成标准：
- 当前行为可稳定复现。

状态：
- 已完成。

### Step 1：`Decode -> Rename` 边界
- 在 [IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h) 中定义 `DecRenInst`
- 将 `DecRenIO` 从 `InstInfo` 改为 `DecRenInst`
- 修改 [Idu.cpp](/home/tututu/qimeng/simulator/back-end/Idu.cpp) 生产该结构
- 修改 [Ren.cpp](/home/tututu/qimeng/simulator/back-end/Ren.cpp) 消费该结构

完成标准：
- `DecRenIO` 不再使用 `InstInfo`
- 这条路径不再依赖通用 `filter()`
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成。

### Step 2：`Rename -> Dispatch` 边界
- 定义 `RenDisInst`
- 将 `RenDisIO` 从 `InstInfo` 改为 `RenDisInst`
- 修改生产端与消费端

完成标准：
- `RenDisIO` 不再使用 `InstInfo`
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成。

### Step 3：`Dispatch -> Issue` 边界
- 定义 `DisIssUop`
- 将 `DisIssIO` 从共享 `MicroOp` 改为专用 `DisIssUop`
- 修改 [Dispatch.cpp](/home/tututu/qimeng/simulator/back-end/Dispatch.cpp) 输出该结构
- 修改 [Isu.cpp](/home/tututu/qimeng/simulator/back-end/Isu.cpp) 在边界处重建本地 `MicroOp`
- 暂时不改 `IssueQueue` 内部存储类型，先保证切口小而稳

完成标准：
- `DisIssIO` 不再直接使用共享 `MicroOp`
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成当前切口。

### Step 4：`Issue -> PRF` 与 `PRF -> EXU` 边界
- 定义：
  - `IssPrfUop`
  - `PrfExeUop`
- 收紧：
  - `IssPrfIO`
  - `PrfExeIO`
- 保持 `IssueQueue` / `Exu` 内部仍可使用本地 `MicroOp`

完成标准：
- 主执行入口路径不再直接用共享 `MicroOp`
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成当前切口。

### Step 5：执行侧请求/回写边界
- 定义并替换：
  - `ExuRobUop`
  - `ExeLsuReqUop`
  - `LsuExeRespUop`
  - 必要时补 `ExePrfWbUop`
- 保持 EXU / LSU 内部仍可重建本地 `MicroOp`

完成标准：
- EXU/LSU/ROB 相关主边界不再直接暴露完整 `MicroOp`
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成。

### Step 6：`Dispatch -> ROB` 与 `ROB -> Commit` 指令级边界
- 定义并替换：
  - `DisRobInst`
  - `RobCommitInst`
- 仅保留 ROB/提交路径实际需要的字段

完成标准：
- 共享 `InstInfo` 不再直接作为这两条边界的接口负载
- 编译通过
- `dhrystone.bin` 结果不变

状态：
- 已完成。

### Step 7：清理旧兼容层
- 清理 [ModuleIOs.h](/home/tututu/qimeng/simulator/back-end/include/ModuleIOs.h)
- 删除残留的 `filter()` 相关定义
- 清理与旧参考接口有关的文档和注释

完成标准：
- backend 主路径不再依赖 `ModuleIOs.h::filter(...)`
- 代码结构与 RTL 参考边界一致

状态：
- 已完成代码清理。
- 文档中仍保留少量历史说明，可后续按需要再删。

## 7. 推荐顺序
1. `DecRenIO`
2. `RenDisIO`
3. `DisIssIO`
4. `IssPrfIO` / `PrfExeIO`
5. `ModuleIOs.h` 清理

## 8. 主要风险
- 消费端可能仍然隐式依赖某个即将从边界删除的字段
- 某些边界当前混杂了真实硬件信号和局部便利字段
- 如果一步改太大，定位回归原因会很困难

## 9. 缓解策略
- 每次只改一条边界
- 每步改动后都编译
- 每步都运行 `./build/simulator ./dhrystone.bin`
- 在边界收紧初期，允许模块内部暂时重建本地 `InstInfo` / `MicroOp`

## 10. 验收标准
- backend 模块间 IO 不再把共享 `InstInfo` / `MicroOp` 当作通用接口类型
- 每条边界都有显式、专用、字段精确的结构体
- `ModuleIOs.h` 不再作为兼容 filter 层参与主路径
- `./build/simulator ./dhrystone.bin` 始终保持稳定基线结果
