# 配置参数文档

本文档介绍当前工程中主要配置参数的生效来源与使用方式。  
其中大部分 back-end 参数来自 `include/config.h`，但 DCache 几何与队列相关参数当前主要由 `MemSubSystem/include/DcacheConfig.h` 生效。

---

## 1. 模拟器控制

### 1.1 日志控制（LOG）

| 参数/宏 | 默认值 | 说明 |
|---------|--------|------|
| `LOG_START` | 0 | 日志开始周期 |
| `LOG_ENABLE` | 未定义 | 取消注释以启用日志 |
| `LOG_MEMORY_ENABLE` | 未定义 | 访存子系统日志域开关 |
| `LOG_DCACHE_ENABLE` | 未定义 | DCache 日志域开关 |
| `LOG_MMU_ENABLE` | 未定义 | MMU/TLB/PTW 日志域开关 |
| `BACKEND_LOG_START` | `LOG_START` | 后端日志起始周期 |
| `MEMORY_LOG_START` | `LOG_START` | 访存日志起始周期 |
| `DCACHE_LOG_START` | `LOG_START` | DCache 日志起始周期 |
| `MMU_LOG_START` | `LOG_START` | MMU 日志起始周期 |

### 1.2 执行限制

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `MAX_COMMIT_INST` | 15,000,000,000 | 0~MAX_UINT64 | 最大提交指令数（定义于 `main.cpp`，不在 `config.h`） |
| `MAX_SIM_TIME` | 1T | 0~MAX_UINT64 | 最大模拟周期数 |

## 2. 功能配置

### 2.1 功能开关（CONFIG_*）

| 宏 | 状态 | 说明 |
|----|------|------|
| `CONFIG_DIFFTEST` | ✅ 启用 | 差分测试功能 |
| `CONFIG_PERF_COUNTER` | ✅ 启用 | 性能计数器 |
| `CONFIG_BPU` | ⚠️ 当前 stock profile 中 `default/small/medium` 启用，`large` 关闭 | 分支预测单元 |
| `CONFIG_TLB_MMU` | ✅ 启用 | I/D 侧统一使用 `TlbMmu` 翻译与 PTW 路径 |

> ICache 模型选择约定：
> - 默认使用真实 ICache 路径（未定义额外宏）
> - 仅在评估性能上界时定义 `USE_IDEAL_ICACHE`
---

## 3. CPU 参数

### 3.1 缓存与内存参数

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `CONFIG_SIM_DDR_LATENCY` | `default: CONFIG_SIM_DDR_LATENCY_CALC (=43)`；`small/medium/large: 50` | 1~500（手动覆盖时） | shared AXI / SimDDR 读延迟（周期数）；default profile 由 DDR 参数自动换算，其余 stock profile 仍固定为 50 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_RESP_LATENCY` | 1 | 0~500 | 最后一个 W beat 握手后，额外等待多少个完整周期，B 通道才首次可见 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_QUEUE_DEPTH` | `CONFIG_AXI_KIT_SIM_DDR_MAX_OUTSTANDING (=32)` | 1~64 | SimDDR 最多可缓存多少笔已接收 AW 但尚未完全完成的写事务 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_ACCEPT_GAP` | 0 | 0~32 | 可选的 W 通道额外节流旋钮；主要用于 stress/debug，不是 stock 主模型 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_DATA_FIFO_DEPTH` | 8 | 1~64 | SimDDR 写数据缓冲深度；FIFO credit 用尽时会对 W 通道施加 backpressure |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_DRAIN_GAP` | 0 | 0~32 | 后端每 drain 一个写 beat 后，额外等待多少个完整周期才继续处理下一个 beat |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_DRAIN_HIGH_WATERMARK` | `CONFIG_AXI_KIT_SIM_DDR_WRITE_DATA_FIFO_DEPTH (=8)` | 1~64 | 写数据 FIFO 占用达到该高水位后，SimDDR 进入 write-drain mode，持续 drain 已缓存写数据 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_DRAIN_LOW_WATERMARK` | 0 | 0~63 | write-drain mode 的退出阈值；FIFO 占用降到该低水位及以下时，`WREADY` 才重新开放 |
| `CONFIG_AXI_KIT_SIM_DDR_READ_TO_WRITE_TURNAROUND` | 0 | 0~64 | 从 read burst service window 切到 write-drain window 前，额外等待的完整周期数 |
| `CONFIG_AXI_KIT_SIM_DDR_WRITE_TO_READ_TURNAROUND` | 0 | 0~64 | 从 write-drain window 切回 read burst service window 前，额外等待的完整周期数 |
| `CONFIG_AXI_KIT_SIM_DDR_BEAT_BYTES` | 32（all profiles） | 4/8/16/32 | 每个 DDR beat 传输字节数 |
| `VIRTUAL_MEMORY_LENGTH` | 1GB | 256MB~8GB | 虚拟内存大小 |
| `PHYSICAL_MEMORY_LENGTH` | 1GB | 256MB~8GB | 物理内存大小 |

> [!NOTE]
> `ICACHE_MISS_LATENCY` 已不再是当前主线 simulator 的 live 配置入口。
> 真实 icache miss 与 shared LLC miss 的外存延迟统一由 shared AXI / SimDDR 配置建模。

> [!NOTE]
> `CONFIG_AXI_KIT_SIM_DDR_WRITE_RESP_LATENCY` 当前是功能模型里的 `B` 通道可见性旋钮，
> 不是精确 DDR 控制器时序模型。当前写路径主线更接近“有限 write-data FIFO +
> 高/低水位驱动的 bursty write-drain mode + 可配置 read/write turnaround”的近似模型；
> `CONFIG_AXI_KIT_SIM_DDR_WRITE_ACCEPT_GAP` 只保留为 stress/debug 旋钮。

#### 3.1.1 DDR 读延迟参数化模型

`include/config.h.default` 中的 `CONFIG_SIM_DDR_LATENCY` 已由固定值改为“参数化计算值”，计算流程如下：

1. 非 DDR 核心延迟（单位 ns）：
   `CONFIG_DDR_SOC_LATENCY_NS + CONFIG_DDR_CDC_LATENCY_NS + CONFIG_DDR_CTL_LATENCY_NS + CONFIG_DDR_PHY_LATENCY_NS`
2. DDR 核心参数：
   - `CONFIG_DDR_CORE_FREQ_MHZ`（DDR core 频率，用于计算 `tCK`）
   - `CONFIG_DDR_CL / CONFIG_DDR_TRCD / CONFIG_DDR_TRP`
   - `CONFIG_DDR_BURST_TRANSFER_BEATS`（默认 4，对应 BL8 传输）
3. workload 分布（百分比）：
   `CONFIG_DDR_PAGE_HIT_RATE_PCT / CONFIG_DDR_PAGE_EMPTY_RATE_PCT / CONFIG_DDR_PAGE_MISS_RATE_PCT`
4. CPU 周期换算：
   `CONFIG_CPU_FREQ_MHZ` 决定单周期时长，最终将平均读延迟从 ns 换算为周期数并四舍五入。

核心参数列表（`include/config.h.default`）：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `CONFIG_CPU_FREQ_MHZ` | 500 | CPU 频率（MHz） |
| `CONFIG_DDR_SOC_LATENCY_NS` | 20 | SoC 非 DDR 核心路径延迟（ns） |
| `CONFIG_DDR_CDC_LATENCY_NS` | 12 | CDC 延迟（ns） |
| `CONFIG_DDR_CTL_LATENCY_NS` | 15 | DDR Controller 延迟（ns） |
| `CONFIG_DDR_PHY_LATENCY_NS` | 13 | DDR PHY 延迟（ns） |
| `CONFIG_DDR_CORE_FREQ_MHZ` | 1600 | DDR core 频率（MHz） |
| `CONFIG_DDR_CL` | 22 | CAS Latency（cycles） |
| `CONFIG_DDR_TRCD` | 22 | tRCD（cycles） |
| `CONFIG_DDR_TRP` | 22 | tRP（cycles） |
| `CONFIG_DDR_BURST_TRANSFER_BEATS` | 4 | DDR burst 数据传输 beats |
| `CONFIG_DDR_PAGE_HIT_RATE_PCT` | 50 | 页命中率（%） |
| `CONFIG_DDR_PAGE_EMPTY_RATE_PCT` | 30 | 页空率（%） |
| `CONFIG_DDR_PAGE_MISS_RATE_PCT` | 20 | 页冲突率（%） |
| `CONFIG_SIM_DDR_LATENCY_CALC` | 自动计算 | 参数化模型算出的最终读延迟（cycles） |

> [!IMPORTANT]
> 默认参数（500MHz CPU、DDR4-3200 对应 1600MHz core、22-22-22、50/30/20）下：
> `CONFIG_SIM_DDR_LATENCY_CALC = 43`。
>
> 当配置为 DDR4-2400 常见时序（1200MHz core、17-17-17，其他不变）时：
> `CONFIG_SIM_DDR_LATENCY_CALC = 44`。

> [!NOTE]
> `CONFIG_SIM_DDR_LATENCY` 仍保留 `#ifndef` 入口：
> 若外部以编译宏显式定义 `CONFIG_SIM_DDR_LATENCY`，将覆盖自动计算值。

> [!NOTE]
> 当前只有 `include/config.h.default` 使用参数化 `CONFIG_SIM_DDR_LATENCY_CALC`；
> `include/config.h.small` / `medium` / `large` 仍保持固定 `50 cycle` 的 stock 默认值。

#### 3.1.2 DCache 参数生效来源（重点标记）

> [!WARNING]
> 当前 DCache 参数存在“双源定义”：`include/config.h` 与 `MemSubSystem/include/DcacheConfig.h`。  
> 代码实际在 DCache 主路径（`RealDcache/MSHR/WriteBuffer/MemSubsystem`）中主要使用 `DcacheConfig.h` 宏。

| 参数族 | 当前主要生效源 | 当前值（代码） | 标记 | 说明 |
|------|------|------|------|------|
| 几何参数：`DCACHE_SETS / DCACHE_WAYS / DCACHE_LINE_SIZE / DCACHE_WORD_NUM` | `MemSubSystem/include/DcacheConfig.h` | `256 / 4 / 64 / 16` | `实际生效` | DCache 数组维度、索引拆解、行大小均依赖这些宏 |
| 队列参数：`DCACHE_MSHR_ENTRIES / DCACHE_WB_ENTRIES` | `MemSubSystem/include/DcacheConfig.h` | `8 / 8`（默认） | `实际生效` | 通过 `CONFIG_DCACHE_MSHR_ENTRIES`、`CONFIG_DCACHE_WB_ENTRIES` 可在编译时覆盖 |
| `DCACHE_LINE_SIZE / DCACHE_WAY_NUM / DCACHE_SET_NUM / DCACHE_WORD_NUM / DCACHE_MAX_PENDING_REQS` | `include/config.h` | 见 `config.h` | `当前未直接驱动 DCache 主实现` | 主要用于全局配置与静态检查，不是当前 MemSubSystem DCache 的主读取来源 |

建议：
1. 调整 DCache 几何时，优先修改 `MemSubSystem/include/DcacheConfig.h`（或对应编译宏）。
2. 修改后同步检查 `include/config.h` 的同名参数，避免文档值与行为值不一致。

#### 3.1.3 Shared AXI / LLC 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `CONFIG_AXI_KIT_MAX_OUTSTANDING` | 32 | shared AXI 全局 read outstanding 上限 |
| `CONFIG_AXI_KIT_MAX_READ_OUTSTANDING_PER_MASTER` | 32 | 每个 master 的 read outstanding 上限 |
| `CONFIG_AXI_KIT_MAX_WRITE_OUTSTANDING` | 32 | interconnect 上游 write outstanding 上限 |
| `CONFIG_AXI_KIT_MAX_WRITE_TRANSACTION_BYTES` | 64B | 单次上游写事务允许的最大 payload 字节数；编译期要求覆盖 I/D cache line 大小 |
| `CONFIG_AXI_KIT_AXI_ID_WIDTH` | 6 | shared AXI ID 位宽；编译期要求足以覆盖 read/write outstanding 上界 |
| `CONFIG_AXI_KIT_SIM_DDR_MAX_OUTSTANDING` | 32 | SimDDR 可接收的 outstanding 上限 |
| `CONFIG_AXI_LLC_SIZE_BYTES` | 8MB | LLC 总容量 |
| `CONFIG_AXI_LLC_WAYS` | 16 | LLC associativity |
| `CONFIG_AXI_LLC_MSHR_NUM` | 8 | LLC 全局共享 MSHR 数量 |
| `CONFIG_AXI_LLC_LOOKUP_LATENCY` | 3 | LLC lookup 响应可见延迟 |
| `CONFIG_AXI_LLC_DCACHE_READ_MISS_NOALLOC` | 0 | DCache demand read miss 是否绕过 LLC install；`0=allocate`，`1=noallocate` |

> [!IMPORTANT]
> 当 parent simulator 集成 `axi-interconnect-kit` 时，上述 `CONFIG_AXI_KIT_*`
> 参数构成编译期契约。若 `config.h` 缺失关键定义，submodule 会直接编译失败，
> 不再静默回落到 submodule 内部默认值。

### 3.2 流水线宽度

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `FETCH_WIDTH` | 16 | 4~32 | 前端取指宽度 |
| `COMMIT_WIDTH` | =`DECODE_WIDTH`（当前为 8） | 2~32 | 提交宽度 |

> [!WARNING]
> `FETCH_WIDTH` 应为 2 的幂次方；同时需满足 `DECODE_WIDTH <= FETCH_WIDTH`，且当前实现要求 `ROB_BANK_NUM == DECODE_WIDTH`。

### 3.3 寄存器堆与分支配置

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ARF_NUM` | 32 | 32 | 架构寄存器数量（RISC-V 固定为32） |
| `PRF_NUM` | 512 | 64~512 | 物理寄存器堆大小 |
| `MAX_BR_NUM` | 64 | 1~64 | 最大同时在飞分支数 |
| `MAX_BR_PER_CYCLE` | 8 | 1~16 | 每周期最大分支处理数 |
| `CSR_NUM` | 21 | 21 | CSR 寄存器数量 |

> [!IMPORTANT]
> - `PRF_NUM` 必须 **≥ ARF_NUM**
> - `MAX_BR_NUM` 不能超过 **64**（受限于位掩码宽度）

### 3.4 ROB 配置

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ROB_NUM` | 512 | 64~1024 | ROB 条目总数 |
| `ROB_BANK_NUM` | =`DECODE_WIDTH`（当前为 8） | 2~32 | ROB Bank 数量 |
| `ROB_LINE_NUM` | 64 | 自动计算 | ROB 行数 = ROB_NUM / ROB_BANK_NUM |

> [!CAUTION]
> `ROB_NUM` 必须是 `ROB_BANK_NUM` 的整数倍！

### 3.5 执行单元配置

#### 3.5.1 功能单元数量

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ALU_NUM` | 6 (自动计算) | 2~16 | ALU 端口数量 |
| `BRU_NUM` | 2 (自动计算) | 1~4 | 分支单元数量 |
| `LSU_LDU_COUNT` | 3 (自动计算) | 1~8 | Load 单元数量 |
| `LSU_STA_COUNT` | 2 (自动计算) | 1~4 | Store Address 单元数量 |
| `LSU_STD_COUNT` | 2 (自动计算) | 1~4 | Store Data 单元数量 |
| `ITLB_ENTRIES` | 32 | 8~128(建议) | 前端 ITLB 表项数 |
| `DTLB_ENTRIES` | 32 | 8~128(建议) | 后端 DTLB 表项数 |

> [!NOTE]
> 这些值由 `GLOBAL_ISSUE_PORT_CONFIG` 中的端口配置自动计算。

#### 3.5.2 队列大小

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `STQ_SIZE` | 64 | 16~256 | Store Queue 容量 |
| `LDQ_SIZE` | 64 | 16~256 | 最大同时在飞 Load 数 |

#### 3.5.3 执行延迟

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `MUL_MAX_LATENCY` | 2 | 1~5 | 乘法指令延迟（周期） |
| `DIV_MAX_LATENCY` | 18 | 10~32 | 除法指令延迟（周期） |

### 3.6 发射端口配置

`GLOBAL_ISSUE_PORT_CONFIG` 定义了所有发射端口及其支持的操作类型：

```cpp
enum { ISSUE_PORT_COUNTER_BASE = __COUNTER__ };
#define PORT_CFG(mask) {(__COUNTER__ - ISSUE_PORT_COUNTER_BASE - 1), (mask)}

constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    PORT_CFG(OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR), // Port 0
    PORT_CFG(OP_MASK_ALU | OP_MASK_DIV | OP_MASK_FP),  // Port 1
    PORT_CFG(OP_MASK_ALU),                              // Port 2
    PORT_CFG(OP_MASK_ALU),                              // Port 3
    PORT_CFG(OP_MASK_ALU),                              // Port 4
    PORT_CFG(OP_MASK_ALU),                              // Port 5
    PORT_CFG(OP_MASK_LD),                               // Port 6
    PORT_CFG(OP_MASK_LD),                               // Port 7
    PORT_CFG(OP_MASK_LD),                               // Port 8
    PORT_CFG(OP_MASK_STA),                              // Port 9
    PORT_CFG(OP_MASK_STA),                              // Port 10
    PORT_CFG(OP_MASK_STD),                              // Port 11
    PORT_CFG(OP_MASK_STD),                              // Port 12
    PORT_CFG(OP_MASK_BR),                               // Port 13
    PORT_CFG(OP_MASK_BR)                                // Port 14
};
#undef PORT_CFG
```

> [!IMPORTANT]
> **CSR 指令硬绑定于 Port 0**，修改配置时必须确保 Port 0 包含 `OP_MASK_CSR`。
> **需要注意PORT的顺序，同类FU的PORT需要连续，IQ会按照BASE+NUM的方式去绑定PORT`。

### 3.7 发射队列配置

`GLOBAL_IQ_CONFIG` 定义了各发射队列的参数：

| IQ 类型 | 容量 | Dispatch 宽度 | 端口数 |
|---------|------|---------------|--------|
| IQ_INT (ALU/MUL/DIV/CSR) | 128 | DECODE_WIDTH | 6 |
| IQ_LD (Load) | 64 | DECODE_WIDTH | 3 |
| IQ_STA (Store Addr) | 64 | DECODE_WIDTH | 2 |
| IQ_STD (Store Data) | 64 | DECODE_WIDTH | 2 |
| IQ_BR (Branch) | 64 | DECODE_WIDTH | 2 |

### 3.8 样例配置

以下示例直接对应当前三套配置文件：

1. `include/config.h.small`
2. `include/config.h.medium`
3. `include/config.h.large`

#### 3.8.1 small 配置（`config.h.small`）

```cpp
constexpr int FETCH_WIDTH = 4;
constexpr int COMMIT_WIDTH = DECODE_WIDTH;
constexpr int PRF_NUM = 64;
constexpr int ROB_NUM = 64;
constexpr int ROB_BANK_NUM = DECODE_WIDTH;
constexpr int MAX_BR_NUM = 16;
constexpr int STQ_SIZE = 16;
constexpr int LDQ_SIZE = 16;

// IQ: INT=32, LD=16, STA=16, STD=16, BR=16
```

端口拓扑（`GLOBAL_ISSUE_PORT_CONFIG`）：
- `ALU/MUL/CSR` x1
- `ALU/DIV/FP` x1
- `LD` x1
- `STA` x1
- `STD` x1
- `BR` x1

#### 3.8.2 medium 配置（`config.h.medium`）

```cpp
constexpr int FETCH_WIDTH = 8;
constexpr int COMMIT_WIDTH = DECODE_WIDTH;
constexpr int PRF_NUM = 160;
constexpr int ROB_NUM = 128;
constexpr int ROB_BANK_NUM = DECODE_WIDTH;
constexpr int MAX_BR_NUM = 32;
constexpr int STQ_SIZE = 32;
constexpr int LDQ_SIZE = 32;

// IQ: INT=64, LD=32, STA=32, STD=32, BR=32
```

端口拓扑（`GLOBAL_ISSUE_PORT_CONFIG`）：
- `ALU/MUL/CSR` x1
- `ALU/DIV/FP` x1
- `ALU` x2
- `LD` x2
- `STA` x2
- `STD` x2
- `BR` x2

#### 3.8.3 large 配置（`config.h.large`）

```cpp
constexpr int FETCH_WIDTH = 16;
constexpr int COMMIT_WIDTH = DECODE_WIDTH;
constexpr int PRF_NUM = 512;
constexpr int ROB_NUM = 512;
constexpr int ROB_BANK_NUM = DECODE_WIDTH;
constexpr int MAX_BR_NUM = 64;
constexpr int STQ_SIZE = 64;
constexpr int LDQ_SIZE = 64;

// IQ: INT=128, LD=64, STA=64, STD=64, BR=64
```

端口拓扑（`GLOBAL_ISSUE_PORT_CONFIG`）：
- `ALU/MUL/CSR` x1
- `ALU/DIV/FP` x1
- `ALU` x4
- `LD` x3
- `STA` x2
- `STD` x2
- `BR` x2

### 3.9 配置调整指南

#### 3.9.1 调整发射宽度时需同步修改

1. **`FETCH_WIDTH`** / **`COMMIT_WIDTH`**
2. **`ROB_BANK_NUM`** - 当前实现中与 `DECODE_WIDTH` 绑定（`ROB_BANK_NUM == DECODE_WIDTH`）
3. **`GLOBAL_ISSUE_PORT_CONFIG`** - 端口数量和类型
4. **`GLOBAL_IQ_CONFIG`** - 各 IQ 的 dispatch_width 和 port_num

#### 3.9.2 资源配比建议

| 资源 | small | medium | large |
|------|--------|--------|----------|
| FETCH_WIDTH | 4 | 8 | 16 |
| PRF_NUM | 64 | 160 | 512 |
| ROB_NUM | 64 | 128 | 512 |
| STQ_SIZE | 16 | 32 | 64 |
| IQ_INT Size | 32 | 64 | 128 |
| IQ_LD Size | 16 | 32 | 64 |

#### 3.9.3 注意事项

1. **编译时断言**：配置错误会在编译阶段被 `static_assert` 捕获
2. **Port 0 特殊性**：CSR 指令必须在 Port 0 执行
3. **位宽限制**：`MAX_BR_NUM` 不能超过 64
4. **性能权衡**：更多的发射端口带来更高的 IPC，但也增加面积和功耗
