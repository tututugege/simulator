# LOG 配置收敛计划

## 背景

当前 [config.h](/home/tututu/qimeng/simulator/include/config.h) 中同时存在两套日志/调试体系：

1. 旧体系
- `LOG_ENABLE`
- `LOG_MEMORY_ENABLE`
- `LOG_DCACHE_ENABLE`
- `LOG_MMU_ENABLE`
- `BACKEND_LOG`
- `MEM_LOG`
- `DCACHE_LOG`
- `MMU_LOG`
- `LOG`
- `DEBUG`

2. 新体系
- `SIM_DEBUG_PRINT`
- `SIM_LSU_MEM_DEBUG_PRINT`
- `SIM_DEBUG_PRINT_CYCLE_BEGIN/END`
- `SIM_LSU_MEM_DEBUG_PRINT_CYCLE_BEGIN/END`
- `SIM_DEBUG_PRINT_ACTIVE`
- `SIM_LSU_MEM_DEBUG_PRINT_ACTIVE`
- `DBG_PRINTF`
- `LSU_MEM_DBG_PRINTF`
- `LSU_MEM_DBG_FPRINTF`

此外，还混有一批“调试相关但不是 print 开关”的配置，例如：
- `CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN`
- `CONFIG_DIFF_DEBUG_MEMTRACE_*`
- `CONFIG_DEADLOCK_REPLAY_TRACE_*`
- `CONFIG_DEBUG_SATP_WRITE_LOG_MAX`
- `CONFIG_DEBUG_PTW_WALK_RESP_DETAIL_MAX`
- `LSU_LIGHT_ASSERT`

这导致 `config.h` 中日志相关宏数量过多，语义边界不清楚。

## 当前现状

### 仍在使用的旧体系

旧体系并不是死代码，当前仍有实际引用：

- [util.h](/home/tututu/qimeng/simulator/back-end/include/util.h)
  - `BE_LOG`
  - `MEM_LOGF`
  - `DCACHE_LOGF`
  - `MMU_LOGF`
- [main.cpp](/home/tututu/qimeng/simulator/main.cpp)
  - `if (LOG) ...`
- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp)
  - `if (LOG && ...) ...`
- [SimDDR.cpp](/home/tututu/qimeng/simulator/axi-interconnect-kit/sim_ddr/SimDDR.cpp)
  - `if (DCACHE_LOG) ...`

因此，旧体系不能直接从 `config.h` 里删除。

### 仍在使用的新体系

新体系也不是死代码，当前仍有实际引用：

- [config.h](/home/tututu/qimeng/simulator/include/config.h)
  - `DBG_PRINTF`
  - `LSU_MEM_DBG_PRINTF`
  - `LSU_MEM_DBG_FPRINTF`
- [main.cpp](/home/tututu/qimeng/simulator/main.cpp)
  - `#if SIM_LSU_MEM_DEBUG_PRINT`
- 多处 `MemSubsystem` / `LSU` / `WriteBuffer` 调试打印

因此，新体系也不能直接删除。

### 已确认可删的死项

本轮已经删除：
- `DEBUG_ADDR`
- `CONFIG_DTLB`
- `CONFIG_ITLB`
- 重复的 `extern long long sim_time`

## 目标

将当前 LOG 配置收敛为：

1. 只有一套主 print 开关体系
2. 域划分明确
3. cycle-window 控制方式统一
4. “print 开关”和“诊断功能/缓冲区大小”分层
5. 尽量减少对已有代码的大范围修改

## 建议的最终结构

建议保留一套统一日志体系，命名类似：

- `SIM_LOG_ENABLE`
- `SIM_LOG_DOMAIN_BACKEND`
- `SIM_LOG_DOMAIN_MEM`
- `SIM_LOG_DOMAIN_DCACHE`
- `SIM_LOG_DOMAIN_MMU`
- `SIM_LOG_CYCLE_BEGIN`
- `SIM_LOG_CYCLE_END`

再在此基础上统一定义：

- `SIM_LOG_BACKEND_ACTIVE`
- `SIM_LOG_MEM_ACTIVE`
- `SIM_LOG_DCACHE_ACTIVE`
- `SIM_LOG_MMU_ACTIVE`

以及统一打印宏：

- `SIM_LOG_BACKEND_PRINTF(...)`
- `SIM_LOG_MEM_PRINTF(...)`
- `SIM_LOG_DCACHE_PRINTF(...)`
- `SIM_LOG_MMU_PRINTF(...)`

如果仍需保留 LSU/MemSubsystem 独立通道，可以额外保留：

- `SIM_LOG_DOMAIN_LSU_MEM`
- `SIM_LOG_LSU_MEM_PRINTF(...)`

但应避免再出现另一整套并行开关。

## 收敛策略

### 第 1 步：先统一旧体系和新体系的“总开关”语义

目标：
- 不改调用点，只改宏定义层

做法：
- 选定一套作为“真值源”
- 另一套仅做兼容 alias

建议：
- 以旧体系为主更省事，因为 `axi-interconnect-kit` 也在吃 `DCACHE_LOG`
- 即：
  - 保留 `LOG / MEM_LOG / DCACHE_LOG / MMU_LOG`
  - 让 `SIM_DEBUG_PRINT_ACTIVE` 和 `SIM_LSU_MEM_DEBUG_PRINT_ACTIVE` 向这套兼容，或反过来由统一新名派生

这样可以先消灭“双源配置”问题。

### 第 2 步：把调用点逐步收敛到统一 helper

目标：
- 代码里不再直接写 `if (LOG) printf(...)`
- 不再同时混用 `DBG_PRINTF` 和 `BE_LOG`

建议统一到 [util.h](/home/tututu/qimeng/simulator/back-end/include/util.h) 这一层 helper：

- `BE_LOG`
- `MEM_LOGF`
- `DCACHE_LOGF`
- `MMU_LOGF`

然后：
- 将 [main.cpp](/home/tututu/qimeng/simulator/main.cpp)
- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp)
- 零散 `if (LOG)` 调用点

改为统一 helper。

完成这一步后，`LOG` / `DEBUG` 这类老别名就可以开始弱化。

### 第 3 步：收缩 cycle-window 配置

当前有两组窗口：
- `BACKEND_LOG_START`
- `MEMORY_LOG_START`
- `DCACHE_LOG_START`
- `MMU_LOG_START`

以及：
- `SIM_DEBUG_PRINT_CYCLE_BEGIN/END`
- `SIM_LSU_MEM_DEBUG_PRINT_CYCLE_BEGIN/END`

建议最后只保留一类窗口模型：

方案 A：全局窗口
- `SIM_LOG_CYCLE_BEGIN`
- `SIM_LOG_CYCLE_END`

方案 B：全局窗口 + 少量域 override
- `SIM_LOG_CYCLE_BEGIN/END`
- `SIM_LOG_MEM_CYCLE_BEGIN/END`

如果主要目标是“配置简单”，优先选方案 A。

### 第 4 步：把 print 宏和诊断配置拆层

这些宏不应和 LOG print 开关混在一起：
- `CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN`
- `CONFIG_DIFF_DEBUG_MEMTRACE_*`
- `CONFIG_DEADLOCK_REPLAY_TRACE_*`
- `CONFIG_DEBUG_SATP_WRITE_LOG_MAX`
- `CONFIG_DEBUG_PTW_WALK_RESP_DETAIL_MAX`
- `LSU_LIGHT_ASSERT`

建议在 `config.h` 中拆成独立小节：

1. `Print Logging`
2. `Trace Buffer / Dump Control`
3. `Runtime Diagnostics / Invariant Checks`

这样后续用户修改“跑得快一些”的配置时，不会误碰这些诊断项。

## 建议的实施顺序

1. 删除已确认死项
- 已完成

2. 统一调用入口
- 把零散 `if (LOG)` 改成统一 helper

3. 选定一套主日志语义
- 建议先保留旧体系名，兼容现有 submodule 使用

4. 收掉重复窗口配置
- 减少 `*_START` 和 `SIM_*_BEGIN/END` 并存

5. 最后再重命名
- 在行为稳定后，再把宏名整理得更统一

## 不建议立即做的事

1. 不建议现在直接删除旧体系
- 因为 `axi-interconnect-kit` 和若干主仓库代码还在直接使用它

2. 不建议现在直接删除新体系
- 因为 `LSU/MemSubsystem` 域已经大量用了 `DBG_PRINTF/LSU_MEM_DBG_PRINTF`

3. 不建议把所有“调试功能”都并进 LOG
- `trace buffer`、`assert`、`io clear` 这类不属于同一层次

## 最小可行下一步

如果下一轮要开始真正清理，建议只做这一个最小动作：

1. 将所有直接 `if (LOG)` 的调用点改成统一 helper
2. 暂时不改 `config.h` 的宏名

这样能先把“调用面”收敛，后续再动配置定义时风险最低。
