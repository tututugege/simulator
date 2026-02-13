# RISC-V 乱序执行模拟器

## 1. 项目简介
本模拟器是一个高性能**周期精确 (Cycle-Accurate)**、**IO精确 (IO-Accurate)** 的 RISC-V 处理器仿真平台，实现了**七级流水线乱序执行 (Out-of-Order Execution)** 微架构。

它旨在为系统软件开发、架构探索以及性能分析提供一个精确且易用的仿真环境，支持从基础指令集校验到复杂操作系统启动的全流程仿真。

![系统架构](./arch.png)

### 核心特性
- **流水线设计**：采用七级流水线结构（Decode -> Rename -> Dispatch -> Issue/RegRead -> Execute -> WriteBack -> Commit）。
- **标准支持**：完整支持 RISC-V **RV32IMASU** 指令集，具备运行原生 **Linux** 的能力。
- **微架构组件**：包含 FTQ (Fetch Target Queue)、ROB (Reorder Buffer)、动态分支预测等关键模块。
- **性能分析**：内置自顶向下微架构分析方法 (**TMA**)，量化 Frontend/Backend Bound、Bad Speculation 及 Retiring 指标。

---

## 2. 仿真运行模式
模拟器支持四种核心运行模式，通过 `-m` 或 `--mode` 参数切换：

- **正常运行 (RUN)**: 从 0x80000000 开始执行完整的乱序流水线模拟（默认模式）。
  - 例子：`./a.out path/to/binary.bin`
- **快照恢复 (CKPT)**: 从指定的 .gz 快照文件恢复系统状态并继续执行。
  - 例子：`./a.out --mode ckpt path/to/checkpoint.gz`
- **混合快进 (FAST)**: 先使用简单的参考模型快速跳过启动阶段，再切换到乱序模拟器执行。
  - 例子：`./a.out --mode fast -f 1000000 path/to/binary.bin`
- **参考模型 (REF)**: 仅运行轻量级参考模型 (Ref Model)，用于功能校验。
  - 例子：`./a.out --mode ref path/to/binary.bin`

---

## 3. 测试程序与基准测试
测试程序位于 `baremetal/` 目录下，主要分为：

- **小程序测试集**：移植自[一生一芯项目](file:///home/tututu/qimeng/simulator/baremetal/test/cpu-tests)，共 35 个功能测试用例。
  - 运行方式：在 `baremetal/test` 目录下执行 `make run`。
- **性能基准测试**：包含 **Coremark** (`baremetal/new_coremark`) 与 **Dhrystone** (`baremetal/new_dhrystone`)。

![测试展示](./all.png)

---

## 4. 验证展示：启动 Linux
模拟器具备运行全功能操作系统的能力，实现了对 MMU、特权级切换及中断/异常处理的完整模拟。

![Linux 启动截图](./Linux.png)

---

## 5. 开发与配置说明
- **代码结构**：
  - `back-end/`: 包含各模块的时序逻辑 (`seq`) 和组合逻辑 (`comb`)。
  - `back-end/include/IO.h`: 定义模块间接口。
  - `diff/`: 用于与参考模型 (Ref_cpu) 进行时钟级对标。
- **配置参数**：
  - 修改 `./back-end/include/config.h` 中的 `LOG` 可切换调试信息的打印。
  - `MAX_SIM_TIME` 用于配置仿真超时的周期上限。
- **详细设计文档**：
  - 请参阅 `doc/modules/` 目录下的各模块设计文档（如 `FTQ_Design.md`, `TMA_Analysis.md`）。
