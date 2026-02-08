# 配置参数文档

本文档介绍 `back-end/include/config.h` 中的各个可配置参数，包括参数说明、可调范围及注意事项，并提供四发射、八发射和十六发射处理器的样例配置。

---

## 1. 系统配置参数

### 1.1 缓存与内存延迟

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ICACHE_MISS_LATENCY` | 100 | 1~500 | ICache 缺失延迟（周期数） |
| `VIRTUAL_MEMORY_LENGTH` | 1GB | 256MB~8GB | 虚拟内存大小 |
| `PHYSICAL_MEMORY_LENGTH` | 1GB | 256MB~8GB | 物理内存大小 |

> [!NOTE]
> `ICACHE_MISS_LATENCY` 可通过编译时 `-D` 选项覆盖默认值。

### 1.2 执行限制

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `MAX_COMMIT_INST` | 15,000,000,000 | 1M~无限 | 最大提交指令数 |
| `MAX_SIM_TIME` | 1T | 1M~无限 | 最大模拟周期数 |

### 1.3 流水线宽度

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `FETCH_WIDTH` | 16 | 4~32 | 前端取指宽度 |
| `COMMIT_WIDTH` | =FETCH_WIDTH | 4~32 | 提交宽度（通常与取指宽度相同） |

> [!WARNING]
> `FETCH_WIDTH` 应为 2 的幂次方，且需与 ROB_BANK_NUM 配合使用。

### 1.4 寄存器堆与分支配置

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ARF_NUM` | 32 | 32 | 架构寄存器数量（RISC-V 固定为32） |
| `PRF_NUM` | 256 | 64~512 | 物理寄存器堆大小 |
| `MAX_BR_NUM` | 64 | 16~64 | 最大同时在飞分支数 |
| `MAX_BR_PER_CYCLE` | 8 | 1~16 | 每周期最大分支处理数 |
| `CSR_NUM` | 21 | 21 | CSR 寄存器数量 |

> [!IMPORTANT]
> - `PRF_NUM` 必须 **≥ ARF_NUM**
> - `MAX_BR_NUM` 不能超过 **64**（受限于位掩码宽度）

### 1.5 ROB 配置

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ROB_NUM` | 256 | 64~512 | ROB 条目总数 |
| `ROB_BANK_NUM` | 16 | 4~32 | ROB Bank 数量 |
| `ROB_LINE_NUM` | 16 | 自动计算 | ROB 行数 = ROB_NUM / ROB_BANK_NUM |

> [!CAUTION]
> `ROB_NUM` 必须是 `ROB_BANK_NUM` 的整数倍！

### 1.6 预热与采样

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `WARMUP` | 100,000,000 | 1M~500M | 预热指令数 |
| `SIMPOINT_INTERVAL` | 100,000,000 | 10M~1B | SimPoint 采样间隔 |

---

## 2. 调试配置

### 2.1 日志控制

| 参数/宏 | 默认值 | 说明 |
|---------|--------|------|
| `LOG_START` | 4,582,800 | 日志开始周期 |
| `LOG_ENABLE` | 未定义 | 取消注释以启用日志 |
| `DEBUG` | 0 | 通用调试日志 |
| `MEM_LOG` | 0 | 内存子系统日志 |
| `DCACHE_LOG` | 0 | DCache 日志 |
| `MMU_LOG` | 0 | MMU 日志 |

### 2.2 功能开关

| 宏 | 状态 | 说明 |
|----|------|------|
| `CONFIG_DIFFTEST` | ✅ 启用 | 差分测试功能 |
| `CONFIG_PERF_COUNTER` | ✅ 启用 | 性能计数器 |
| `CONFIG_BPU` | ✅ 启用 | 分支预测单元 |
| `CONFIG_MMU` | ❌ 禁用 | MMU 功能 |
| `CONFIG_CACHE` | ❌ 禁用 | Cache 功能 |
| `CONFIG_LOOSE_VA2PA` | ✅ 启用 | 宽松的地址翻译检查 |

---

## 3. 执行单元配置

### 3.1 功能单元数量

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `ALU_NUM` | 6 (自动计算) | 2~16 | ALU 端口数量 |
| `BRU_NUM` | 2 (自动计算) | 1~4 | 分支单元数量 |
| `LSU_LDU_COUNT` | 4 (自动计算) | 1~8 | Load 单元数量 |
| `LSU_STA_COUNT` | 2 (自动计算) | 1~4 | Store Address 单元数量 |
| `LSU_STD_COUNT` | 2 (自动计算) | 1~4 | Store Data 单元数量 |

> [!NOTE]
> 这些值由 `GLOBAL_ISSUE_PORT_CONFIG` 中的端口配置自动计算。

### 3.2 队列大小

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `STQ_NUM` | 64 | 16~64 | Store Queue 容量 |
| `MAX_INFLIGHT_LOADS` | 64 | 16~64 | 最大同时在飞 Load 数 |

> [!WARNING]
> `STQ_NUM` 不能超过 **64**（受限于位掩码宽度），`MAX_INFLIGHT_LOADS` 不应超过 `STQ_NUM`。

### 3.3 执行延迟

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| `MUL_MAX_LATENCY` | 2 | 1~5 | 乘法指令延迟（周期） |
| `DIV_MAX_LATENCY` | 18 | 10~40 | 除法指令延迟（周期） |

---

## 4. 发射端口配置

`GLOBAL_ISSUE_PORT_CONFIG` 定义了所有发射端口及其支持的操作类型：

```cpp
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR}, // Port 0: ALU+MUL+CSR
    {1, OP_MASK_ALU | OP_MASK_MUL},               // Port 1: ALU+MUL
    {2, OP_MASK_ALU | OP_MASK_DIV},               // Port 2: ALU+DIV
    {3, OP_MASK_ALU},                             // Port 3: ALU
    ...
    {14, OP_MASK_BR},                             // Port 14: Branch
    {15, OP_MASK_BR}                              // Port 15: Branch
};
```

> [!IMPORTANT]
> **CSR 指令硬绑定于 Port 0**，修改配置时必须确保 Port 0 包含 `OP_MASK_CSR`。

---

## 5. 发射队列配置

`GLOBAL_IQ_CONFIG` 定义了各发射队列的参数：

| IQ 类型 | 容量 | Dispatch 宽度 | 端口数 |
|---------|------|---------------|--------|
| IQ_INT (ALU/MUL/DIV/CSR) | 64 | FETCH_WIDTH | 6 |
| IQ_LD (Load) | 32 | FETCH_WIDTH | 4 |
| IQ_STA (Store Addr) | 32 | FETCH_WIDTH | 2 |
| IQ_STD (Store Data) | 32 | FETCH_WIDTH | 2 |
| IQ_BR (Branch) | 32 | FETCH_WIDTH | 2 |

---

## 6. 样例配置

### 6.1 四发射配置 (4-Issue)

适用于低功耗、小面积设计：

```cpp
// 流水线宽度
constexpr int FETCH_WIDTH = 4;
constexpr int COMMIT_WIDTH = 4;

// 资源配置
constexpr int PRF_NUM = 96;
constexpr int ROB_NUM = 64;
constexpr int ROB_BANK_NUM = 4;
constexpr int MAX_BR_NUM = 16;
constexpr int STQ_NUM = 16;
constexpr int MAX_INFLIGHT_LOADS = 16;

// 发射端口配置 (8 ports)
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR}, // Port 0: ALU+MUL+CSR
    {1, OP_MASK_ALU | OP_MASK_DIV},               // Port 1: ALU+DIV
    {2, OP_MASK_LD},                              // Port 2: Load
    {3, OP_MASK_LD},                              // Port 3: Load
    {4, OP_MASK_STA},                             // Port 4: Store Addr
    {5, OP_MASK_STD},                             // Port 5: Store Data
    {6, OP_MASK_BR},                              // Port 6: Branch
    {7, OP_MASK_BR},                              // Port 7: Branch
};

// IQ 配置
constexpr IQStaticConfig GLOBAL_IQ_CONFIG[] = {
    {IQ_INT, 24, 4, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, 0, 2},
    {IQ_LD,  16, 4, OP_MASK_LD, 2, 2},
    {IQ_STA, 16, 4, OP_MASK_STA, 4, 1},
    {IQ_STD, 16, 4, OP_MASK_STD, 5, 1},
    {IQ_BR,  16, 4, OP_MASK_BR, 6, 2},
};
```

**特点**：2 ALU + 2 Load + 1 STA + 1 STD + 2 BR

---

### 6.2 八发射配置 (8-Issue)

适用于中端性能设计：

```cpp
// 流水线宽度
constexpr int FETCH_WIDTH = 8;
constexpr int COMMIT_WIDTH = 8;

// 资源配置
constexpr int PRF_NUM = 160;
constexpr int ROB_NUM = 128;
constexpr int ROB_BANK_NUM = 8;
constexpr int MAX_BR_NUM = 32;
constexpr int STQ_NUM = 32;
constexpr int MAX_INFLIGHT_LOADS = 32;

// 发射端口配置 (12 ports)
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR}, // Port 0: ALU+MUL+CSR
    {1, OP_MASK_ALU | OP_MASK_MUL},               // Port 1: ALU+MUL
    {2, OP_MASK_ALU | OP_MASK_DIV},               // Port 2: ALU+DIV
    {3, OP_MASK_ALU},                             // Port 3: ALU
    {4, OP_MASK_LD},                              // Port 4: Load
    {5, OP_MASK_LD},                              // Port 5: Load
    {6, OP_MASK_LD},                              // Port 6: Load
    {7, OP_MASK_STA},                             // Port 7: Store Addr
    {8, OP_MASK_STD},                             // Port 8: Store Data
    {9, OP_MASK_STD},                             // Port 9: Store Data
    {10, OP_MASK_BR},                             // Port 10: Branch
    {11, OP_MASK_BR},                             // Port 11: Branch
};

// IQ 配置
constexpr IQStaticConfig GLOBAL_IQ_CONFIG[] = {
    {IQ_INT, 48, 8, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, 0, 4},
    {IQ_LD,  24, 8, OP_MASK_LD, 4, 3},
    {IQ_STA, 24, 8, OP_MASK_STA, 7, 1},
    {IQ_STD, 24, 8, OP_MASK_STD, 8, 2},
    {IQ_BR,  24, 8, OP_MASK_BR, 10, 2},
};
```

**特点**：4 ALU + 3 Load + 1 STA + 2 STD + 2 BR

---

### 6.3 十六发射配置 (16-Issue) - 当前默认配置

适用于高性能设计：

```cpp
// 流水线宽度
constexpr int FETCH_WIDTH = 16;
constexpr int COMMIT_WIDTH = 16;

// 资源配置
constexpr int PRF_NUM = 256;
constexpr int ROB_NUM = 256;
constexpr int ROB_BANK_NUM = 16;
constexpr int MAX_BR_NUM = 64;
constexpr int STQ_NUM = 64;
constexpr int MAX_INFLIGHT_LOADS = 64;

// 发射端口配置 (16 ports)
constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {
    {0, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_CSR}, // Port 0
    {1, OP_MASK_ALU | OP_MASK_MUL},               // Port 1
    {2, OP_MASK_ALU | OP_MASK_DIV},               // Port 2
    {3, OP_MASK_ALU},                             // Port 3
    {4, OP_MASK_ALU},                             // Port 4
    {5, OP_MASK_ALU},                             // Port 5
    {6, OP_MASK_LD},                              // Port 6
    {7, OP_MASK_LD},                              // Port 7
    {8, OP_MASK_LD},                              // Port 8
    {9, OP_MASK_LD},                              // Port 9
    {10, OP_MASK_STA},                            // Port 10
    {11, OP_MASK_STA},                            // Port 11
    {12, OP_MASK_STD},                            // Port 12
    {13, OP_MASK_STD},                            // Port 13
    {14, OP_MASK_BR},                             // Port 14
    {15, OP_MASK_BR},                             // Port 15
};

// IQ 配置
constexpr IQStaticConfig GLOBAL_IQ_CONFIG[] = {
    {IQ_INT, 64, 16, OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR, 0, 6},
    {IQ_LD,  32, 16, OP_MASK_LD, 6, 4},
    {IQ_STA, 32, 16, OP_MASK_STA, 10, 2},
    {IQ_STD, 32, 16, OP_MASK_STD, 12, 2},
    {IQ_BR,  32, 16, OP_MASK_BR, 14, 2},
};
```

**特点**：6 ALU + 4 Load + 2 STA + 2 STD + 2 BR

---

## 7. 配置调整指南

### 7.1 调整发射宽度时需同步修改

1. **`FETCH_WIDTH`** / **`COMMIT_WIDTH`**
2. **`ROB_BANK_NUM`** - 通常与 FETCH_WIDTH 相等
3. **`GLOBAL_ISSUE_PORT_CONFIG`** - 端口数量和类型
4. **`GLOBAL_IQ_CONFIG`** - 各 IQ 的 dispatch_width 和 port_num

### 7.2 资源配比建议

| 资源 | 四发射 | 八发射 | 十六发射 |
|------|--------|--------|----------|
| PRF_NUM | 96 | 160 | 256 |
| ROB_NUM | 64 | 128 | 256 |
| STQ_NUM | 16 | 32 | 64 |
| IQ_INT Size | 24 | 48 | 64 |
| IQ_LD Size | 16 | 24 | 32 |

### 7.3 注意事项

1. **编译时断言**：配置错误会在编译阶段被 `static_assert` 捕获
2. **Port 0 特殊性**：CSR 指令必须在 Port 0 执行
3. **位宽限制**：`MAX_BR_NUM` 和 `STQ_NUM` 不能超过 64
4. **性能权衡**：更多的发射端口带来更高的 IPC，但也增加面积和功耗
