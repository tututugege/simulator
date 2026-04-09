# BSD Front-end 等价改写计划书（阶段二）

## 1. 目标与范围

- 目标：将 `front-end/` 主路径改写为 BSD 可交付形态，保持行为等价。
- 范围：按 `front-end/FRONTEND_COMB_SEQ_WRITING_STANDARD.md` 的约束执行。
- 排除：`front-end/icache/` 内部实现、`front-end/FrontTop.cpp`（按规范声明）。

## 2. 验证方法（固定流程）

1. `make clean`
2. `make -j8` 或 `make large -j8`
3. `./build/simulator ./dry.bin`

验证前置：

- `CONFIG_BPU` 必须打开（编译期宏已定义）。
- 运行输出中 `bpu accuracy` 必须不等于 `1`。

## 3. 基线数据（待填写）

> 说明：本节记录 default / large 两个规模在 BPU 打开条件下的 baseline。

### 3.1 default baseline（BPU ON）

- 构建命令：`make clean && make -j8`
- 运行命令：`./build/simulator ./dry.bin`
- `sim-time(cycle)`：`1284054`
- `committed(total/load/store)`：`2400586 / 520129 / 450054`
- `IPC`（`avg inst / sim cycle`）：`1.8702`
- `bpu accuracy`：`0.999850`

### 3.2 large baseline（BPU ON）

- 构建命令：`make clean && make large -j8`
- 运行命令：`./build/simulator ./dry.bin`
- `sim-time(cycle)`：`454432`
- `committed(total/load/store)`：`2400586 / 520129 / 450054`
- `IPC`（`avg inst / sim cycle`）：`5.2869`
- `bpu accuracy`：`0.999824`

## 4. 审计结论（当前）

### 4.1 comb 纯组合

- 阻塞项 A：`btb_pred_read_req_comb` / `btb_upd_read_req_comb` 在 comb 里直读 `mem_bht`。
- 阻塞项 B：`tage_comb` 在 comb 里直读 `scl_table` / `loop_table`。

### 4.2 seq 纯读写

- 阻塞项 C：`tage_seq_write` 内含策略计算（SC-L / loop 更新策略），非纯提交。

### 4.3 清单正确性

- `predecode_comb`、`predecode_checker_comb` 非严格二参。
- `bpu_predict_main_comb` / `bpu_nlp_comb` / `bpu_hist_comb` / `bpu_queue_comb` 非严格二参。

## 5. 分阶段改写计划

### P0：基线与门禁

- 打开 `default/large` 的 `CONFIG_BPU`。
- 记录双规模 baseline（cycle / IPC / bpu accuracy）。
- 建立回归对比基准。

### P1：低风险接口规范化

- 将 `predecode_comb` / `predecode_checker_comb` 改为严格二参。
- 保持行为等价，进行一次 default 快速回归。

### P2：BTB 读边界修复

- 去除 `btb_*_read_req_comb` 对 `mem_bht` 的直读。
- 拆为 request-comb + seq_read + 后级comb 链路。
- 完成后进行一次 default 回归。

### P3：TAGE 边界修复

- 将 `tage_comb` 对 `scl_table` / `loop_table` 的读取迁入 seq_read 快照。
- 将 `tage_seq_write` 中策略推导前移到 comb，`seq_write` 仅提交 req。
- 完成后进行一次 default 回归。

### P4：BPU_TOP 清单函数二参化

- 将 `bpu_predict_main_comb` / `bpu_nlp_comb` / `bpu_hist_comb` / `bpu_queue_comb` 收敛为严格二参。
- 完成后进行一次 default 回归。

### P5：冻结交付与双规模验收

- 更新训练函数清单与交付文档。
- 执行 default + large 最终回归，并与 P0 基线逐项对比。

## 6. 回归频率策略

- 仅在每个高风险阶段结束后跑一次 `dry.bin`，避免过度频繁验证。
- 如出现高风险改造尝试，再加一次临时回归确认无行为漂移。
