# Front-end Config 开关说明手册

本手册覆盖以下两个配置头中的所有可配置项：

- `front-end/config/frontend_feature_config.h`
- `front-end/config/frontend_diag_config.h`

目标：说明每个开关/参数的用途、默认值、建议取值与生效方式。

---

## 1. 如何配置（推荐方式）

### 1.1 通过编译参数临时覆盖

使用 Makefile：

```bash
make clean
make -j8 EXTRA_CXXFLAGS="-DSRAM_DELAY_ENABLE -DBPU_BANK_NUM=8 -DFRONTEND_DISABLE_2AHEAD"
```

常见写法：

- 布尔“存在即开启”宏：`-DMACRO_NAME`
- 数值宏：`-DMACRO_NAME=value`

### 1.2 直接修改配置头默认值

如果你想长期固定某些配置，也可以直接编辑：

- `front-end/config/frontend_feature_config.h`
- `front-end/config/frontend_diag_config.h`

---

## 2. 开关类型约定

- **布尔开关（值型）**：一般 `0/1`，在代码里用 `#if MACRO`。
- **布尔开关（存在型）**：定义即开启，未定义即关闭，代码里常用 `#ifdef MACRO`。
- **数值参数**：表项、位宽、深度、阈值、延迟等。
- **派生宏**：由其它宏自动推导，不建议手动改。

---

## 3. Feature 配置详解

### 3.1 SRAM 延迟相关

- `SRAM_DELAY_ENABLE`（存在型，默认：关闭）
  - 作用：启用 BPU/TAGE/BTB 的 SRAM delay 行为（非零延迟建模）。
  - 开启方式：`-DSRAM_DELAY_ENABLE`。
- `SRAM_DELAY_MIN`（默认：`0`）
  - 作用：随机/区间延迟下限（注释中语义为 n+1）。
- `SRAM_DELAY_MAX`（默认：`0`）
  - 作用：随机/区间延迟上限。
  - 典型：`-DSRAM_DELAY_ENABLE -DSRAM_DELAY_MAX=1` 表示随机延迟最大为 1 的配置。

### 3.2 Front-end/BPU 顶层基础参数

- `SPECULATIVE_ON`（存在型，默认：开启）
  - 作用：启用 speculative 相关路径。
  - 备注：当前头文件内直接定义，如需关闭，需改头文件。
- `COMMIT_WIDTH`（默认：`4`）
  - 作用：后端回传更新通道宽度，影响 front/back 接口数组维度。
- `FETCH_WIDTH`（默认：`4`）
  - 作用：前端每拍取指/传递宽度。
- `BPU_BANK_NUM`（默认：`8`）
  - 作用：BPU bank 数量，影响 bank 选择与并发访问行为。
- `ENABLE_BPU_RAS`（存在型，默认：开启）
  - 作用：对 `BR_RET` 使用 RAS 预测。
  - 关闭效果：回退到 BTB 目标预测路径。
  - 备注：当前头文件内直接定义，如需关闭，需改头文件。
- `RAS_DEPTH`（默认：`64`）
  - 作用：RAS 堆栈深度。
- `BPU_TYPE_ENTRY_NUM`（默认：`4096`）
  - 作用：分支类型表大小。
- `Q_DEPTH`（默认：`5000`）
  - 作用：BPU 更新队列（UQ）深度。

### 3.3 TAGE 参数

- `BASE_ENTRY_NUM`（`2048`）：TAGE base predictor 表项数。
- `GHR_LENGTH`（`256`）：全局历史长度。
- `TN_MAX`（`4`）：TAGE 组件/表路数上限。
- `TN_ENTRY_NUM`（`4096`）：TAGE tagged 表项数。
- `FH_N_MAX`（`3`）：折叠历史组数参数。
- `TAGE_BASE_IDX_WIDTH`（`11`）：base 索引位宽（典型 log2(2048)）。
- `TAGE_TAG_WIDTH`（`8`）：tag 位宽。
- `TAGE_IDX_WIDTH`（`12`）：tagged 表索引位宽（典型 log2(4096)）。
- `ENABLE_TAGE_USE_ALT_ON_NA`（`1`）：启用 provider 不可靠时 fallback 到 alt pred 的策略。
- `TAGE_USE_ALT_USEFUL_THRESHOLD`（`0`）：use_alt 门限。
- `TAGE_PROVIDER_WEAK_LOW`（`3`）：provider 弱预测下界。
- `TAGE_PROVIDER_WEAK_HIGH`（`3`）：provider 弱预测上界。
- `ENABLE_TAGE_SC_LITE`（`1`）：启用轻量 SC-Lite 修正路径。
- `TAGE_SC_ENTRY_NUM`（`1024`）：SC 表项数。
- `TAGE_SC_USE_WEAK_ONLY`（`1`）：SC 仅对弱 provider 生效。
- `TAGE_SC_STRONG_ONLY_OVERRIDE`（`1`）：强预测覆盖策略开关。
- `TAGE_SC_PROVIDER_WEAK_LOW`（默认继承 `TAGE_PROVIDER_WEAK_LOW`）：SC 弱区间下界。
- `TAGE_SC_PROVIDER_WEAK_HIGH`（`4`）：SC 弱区间上界。

### 3.4 BTB/BHT/TC 参数

- `BTB_ENTRY_NUM`（`512`）：BTB 表项数。
- `ENABLE_BTB_ALIAS_HASH`（`1`）：启用 BTB 别名哈希。
- `BTB_TAG_LEN`（`8`）：BTB tag 位宽。
- `BTB_WAY_NUM`（`4`）：BTB 组相联路数。
- `BTB_TYPE_ENTRY_NUM`（`4096`）：分支类型表大小。
- `BHT_ENTRY_NUM`（`2048`）：BHT 表项数。
- `TC_ENTRY_NUM`（`2048`）：Target Cache 表项数。
- `TC_WAY_NUM`（`2`）：TC 组相联路数。
- `TC_TAG_LEN`（`10`）：TC tag 位宽。
- `ENABLE_INDIRECT_BTB_FALLBACK`（`1`）：间接分支预测失败时回退策略开关。
- `ENABLE_INDIRECT_BTB_TRAIN`（`1`）：间接分支训练开关。
- `INDIRECT_BTB_INIT_USEFUL`（`0`）：间接 BTB useful 初值。
- `INDIRECT_TC_INIT_USEFUL`（`1`）：间接 TC useful 初值。
- `ENABLE_TC_TARGET_SIGNATURE`（`1`）：启用 TC target signature 机制。

### 3.5 2-Ahead 与 slot1 自适应门控

- `TWO_AHEAD_TABLE_SIZE`（`4096`）：2-ahead 主表大小。
- `NLP_TABLE_SIZE`（默认等于 `TWO_AHEAD_TABLE_SIZE`）：NLP 表大小。
- `NLP_CONF_BITS`（`2`）：NLP 置信度位宽。
- `NLP_CONF_THRESHOLD`（`2`）：NLP 判定命中门限。
- `NLP_CONF_INIT`（`1`）：NLP 置信度初始化值。
- `ENABLE_2AHEAD_SLOT1_PRED`（`1`）：slot1 的 2-ahead 预测开关。
- `AHEAD_SLOT1_TABLE_SIZE`（默认等于 `TWO_AHEAD_TABLE_SIZE`）：slot1 表大小。
- `AHEAD_SLOT1_CONF_BITS`（`2`）：slot1 置信度位宽。
- `AHEAD_SLOT1_CONF_THRESHOLD`（`2`）：slot1 命中门限。
- `ENABLE_2AHEAD_SLOT1_ADAPTIVE_GATING`（`1`）：slot1 自适应门控开关。
- `AHEAD_GATE_WINDOW`（`512`）：门控统计窗口长度。
- `AHEAD_GATE_DISABLE_THRESHOLD`（`35`）：关闭门控阈值（百分比）。
- `AHEAD_GATE_ENABLE_THRESHOLD`（`60`）：恢复门控阈值（百分比）。
- `FRONTEND_DISABLE_2AHEAD`（存在型，默认：关闭）
  - 作用：全局关闭 2-ahead。
  - 方式：`-DFRONTEND_DISABLE_2AHEAD`。
- `ENABLE_2AHEAD`（存在型，派生）
  - 规则：当 **未定义** `FRONTEND_DISABLE_2AHEAD` 时自动定义。
  - 备注：一般不要直接设它，推荐通过 `FRONTEND_DISABLE_2AHEAD` 控制。

### 3.6 ICache 模式与双请求

- `FRONTEND_ICACHE_MODE`（默认：`1`）
  - `0`：true icache
  - `1`：ideal icache
- `ENABLE_FRONTEND_IDEAL_ICACHE_DUAL_REQ`（默认：`1`）
  - 作用：ideal icache 下是否允许双请求路径。
- `USE_IDEAL_ICACHE`（派生）
  - 在 `FRONTEND_ICACHE_MODE==1` 时自动定义。
- `USE_TRUE_ICACHE`（派生）
  - 在 `FRONTEND_ICACHE_MODE==0` 时自动定义。
- `FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE`（派生，`0/1`）
  - 仅当 `FRONTEND_ICACHE_MODE==1` 且 `ENABLE_FRONTEND_IDEAL_ICACHE_DUAL_REQ==1` 时为 `1`。
- `ICACHE_LINE_SIZE`（`32`，字节）
  - 作用：cache line 大小。
- `ICACHE_MISS_LATENCY`（`100`，cycle）
  - 作用：true icache miss 延迟建模参数。

### 3.7 FIFO 容量参数

- `INSTRUCTION_FIFO_SIZE`（`32`）：指令 FIFO 深度。
- `PTAB_SIZE`（`32`）：PTAB 深度。
- `FETCH_ADDR_FIFO_SIZE`（`32`）：取指地址 FIFO 深度。
- `FRONT2BACK_FIFO_SIZE`（`64`）：front2back FIFO 深度。

---

## 4. Diag（调试/统计）配置详解

- `DEBUG_PRINT`（默认：`0`）
  - 控制 `DEBUG_LOG(...)` 的总开关，适合粗粒度日志。
- `DEBUG_PRINT_SMALL`（`0`）
  - 控制 `DEBUG_LOG_SMALL(...)`。
- `DEBUG_PRINT_SMALL_2`（`0`）
  - 控制 `DEBUG_LOG_SMALL_2(...)`。
- `DEBUG_PRINT_SMALL_3`（`0`）
  - 控制 `DEBUG_LOG_SMALL_3(...)`。
- `DEBUG_PRINT_SMALL_4`（`0`）
  - 控制 `DEBUG_LOG_SMALL_4(...)`。
- `DEBUG_PRINT_SMALL_5`（`0`）
  - 控制 `DEBUG_LOG_SMALL_5(...)`。
- `FRONTEND_ENABLE_RUNTIME_STATS_SUMMARY`（默认：`1`）
  - 作用：控制 `front_top` 退出时 `[FRONT-STATS]` 汇总打印。
- `FRONTEND_ENABLE_TRAINING_AREA_STATS`（默认：`1`）
  - 作用：控制 `--frontend-stats` 的训练/面积统计打印。

---

## 5. 约束与合法性检查（编译期）

以下检查由 `front-end/BPU/BPU_configs.h` 保证：

- `TAGE_SC_ENTRY_NUM` 必须是 2 的幂。
- `TC_TAG_LEN` 必须在 `[1,31]`。
- `TC_WAY_NUM` 必须 `>0`。
- `INDIRECT_BTB_INIT_USEFUL` 必须在 `[0,7]`。
- `INDIRECT_TC_INIT_USEFUL` 必须在 `[0,7]`。
- `NLP_CONF_THRESHOLD <= (1<<NLP_CONF_BITS)-1`。
- `AHEAD_SLOT1_CONF_THRESHOLD <= (1<<AHEAD_SLOT1_CONF_BITS)-1`。
- `AHEAD_GATE_WINDOW > 0`。
- `AHEAD_GATE_DISABLE_THRESHOLD` 与 `AHEAD_GATE_ENABLE_THRESHOLD` 均在 `[0,100]`，且前者 `<=` 后者。
- `FRONTEND_ICACHE_MODE` 只能是 `0` 或 `1`。

---

## 6. 常用配置示例

### 6.1 关闭 2-Ahead

```bash
make clean
make -j8 EXTRA_CXXFLAGS="-DFRONTEND_DISABLE_2AHEAD"
```

### 6.2 启用 SRAM 延迟

```bash
make clean
make -j8 EXTRA_CXXFLAGS="-DSRAM_DELAY_ENABLE"
```

### 6.3 启用随机 SRAM 延迟（max=1）+ bank8

```bash
make clean
make -j8 EXTRA_CXXFLAGS="-DSRAM_DELAY_ENABLE -DSRAM_DELAY_MAX=1 -DBPU_BANK_NUM=8"
```

### 6.4 仅打开前端统计，关闭 debug 日志

```bash
make clean
make -j8 EXTRA_CXXFLAGS="-DFRONTEND_ENABLE_RUNTIME_STATS_SUMMARY=1 -DFRONTEND_ENABLE_TRAINING_AREA_STATS=1 -DDEBUG_PRINT=0 -DDEBUG_PRINT_SMALL=0"
```

---

## 7. 维护建议

- 推荐通过 `EXTRA_CXXFLAGS` 做实验矩阵，不要频繁改默认值。
- 大多数布尔开关建议统一改为 `#ifndef MACRO` + `#define MACRO 0/1` 风格，便于脚本化覆盖。
- 派生宏（如 `USE_TRUE_ICACHE`、`FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE`）不建议手工设置。

