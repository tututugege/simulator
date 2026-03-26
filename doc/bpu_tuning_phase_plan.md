# BPU Tuning 分阶段计划书（先修策略，再扩 TN_MAX）

日期：2026-03-20  
仓库：`simulator-main-mainline`  
当前分支：`bpu-tuning`（HEAD: `be0b1dd`）  
当前状态：工作区干净（`git status --porcelain` 为空）

---

## 0. 执行原则

1. **先修策略，再扩结构**：先保持 `TN_MAX=4` 修正已识别问题，再推进 `TN_MAX=8`。
2. **小步提交、可回退**：每个小阶段至少 1 个独立 commit，必要时加 tag。
3. **验证分级**：
   - 日常快验证：`./build/simulator ./dry.bin`
   - 阶段性大改后：`./build/simulator ./coremark.bin`
   - 编译门禁：`make -j8`
4. **目标定义**：不引入功能错误；可接受小幅性能波动；期望总体性能提升。

---

## 1. 阶段 A：保持 TN_MAX=4，修复策略问题

> 本阶段不改表数量，仅修策略正确性与鲁棒性。

### A1. 更新策略修复（`tage_update_comb` 主路径）

**目标**
- 修复“provider miss 且高表无可分配项时，`useful` 衰减未正确触发”的路径。
- 明确 mispredict 下的分配/衰减优先级，避免长期卡分配。

**计划修改点**
- 调整 allocation fail 分支中 `pcpn==TN_MAX` 的遍历边界与衰减覆盖范围。
- 审核并修正 `useful` 更新与 reset 同周期冲突处理逻辑，保持一致语义。
- 对 `alt_used/provider_raw` 场景补齐注释与判定顺序，避免 credit assignment 歧义。

**完成标准**
- 不改变接口定义情况下，mispredict 后高表可持续获得再训练机会。
- `dry.bin` 行为稳定，无新增断言/崩溃。

**建议提交**
- `commit A1`: `[BPU][TAGE] fix allocation-fail useful aging path`

---

### A2. `use_alt_on` 机制升级（保留兼容）

**目标**
- 将当前“固定阈值 + useful 门限”策略升级为更自适应机制（近似 `useAltPredForNewlyAllocated` 思路）。

**计划修改点**
- 引入轻量运行时计数器（例如 4~6bit 饱和计数器）替代纯静态阈值。
- 仅在 provider 弱、且 provider/alt 分歧时应用选择器更新。
- 保留宏开关以支持回退到旧策略（便于 A/B 对比）。

**完成标准**
- `use_alt_on` 在不同程序段有自适应收敛行为。
- `dry.bin` 通过，且预测路径无功能回退。

**建议提交**
- `commit A2`: `[BPU][TAGE] add adaptive use-alt-on-newly-allocated policy`

---

### A3. SC/Loop 相关训练归因修正

**目标**
- 使 SC/Loop 的“是否参与最终决策”信息进入更新路径，减少错误归因。

**计划修改点**
- 消费 `sc_used_in/loop_used_in` 等 metadata，在 update 中区分：
  - 纯 TAGE 命中修正
  - SC 覆盖
  - Loop 覆盖
- 调整 SC-Lite 与 SC-L 的训练触发条件，避免重复或冲突训练。
- 确认 Loop predictor 在“未被采用”时不应过度强化。

**完成标准**
- SC/Loop 训练与最终输出决策一致（credit assignment 一致）。
- 阶段结束执行一次 `coremark.bin` 验证无功能异常。

**建议提交**
- `commit A3`: `[BPU][TAGE] align SC/Loop training with final predictor decision`

---

## 2. 阶段 B：扩展到 TN_MAX=8（结构改造）

> 在阶段 A 稳定后进入，分“链路适配”与“历史适配”两步。

### B1. 前后端链路 `TN_MAX` 参数化改造

**目标**
- 清理前后端通路中硬编码 `4` 的路径，统一改为依赖 `TN_MAX`。

**计划修改点**
- 排查并修改以下类信号/数组通路：
  - `pcpn/altpcpn` 编码宽度、边界判定
  - `tage_tag_flat[] / tage_idx_flat[]` 相关搬运、锁存、提交
  - UpdateRequest 各路 `*_we/*_wdata` 的循环边界
- 统一前端/后端/公共头文件中的宽度定义来源，避免重复硬编码。
- 若必要，更新 `static_assert` 或编译期检查，防止配置不一致。

**完成标准**
- `TN_MAX` 从 4 提升到 8 后可无编译错误通过 `make -j8`。
- `dry.bin` 能正常运行。

**建议提交**
- `commit B1`: `[BPU][TAGE] generalize fixed-4 metadata/data paths to TN_MAX`

---

### B2. GHR/FH 与历史长度体系扩展

**目标**
- 为 8 张 tagged table 建立合理几何历史长度，并完成折叠历史适配。

**计划修改点**
- 设计 8 档历史长度（几何级数分布），并与 `GHR_LENGTH` 协同校验。
- 扩展 `ghr_length[]`、`fh_length[][]`、索引/标签 folding 路径。
- 检查 SC-L 相关历史采样表是否需同步拉伸（保持尺度匹配）。
- 审核 reset/allocate 节奏是否需微调（表数增加后冲突统计变化）。

**完成标准**
- `TN_MAX=8` 下索引/标签生成、更新、分配路径全部一致且可运行。
- 阶段结束执行 `coremark.bin` 验证功能正确。

**建议提交**
- `commit B2`: `[BPU][TAGE] extend history geometry and folding for TN_MAX=8`

---

## 3. 阶段 C：验证与回归策略

### C1. 每次改动后的最小验证
1. `make -j8`
2. `./build/simulator ./dry.bin`

### C2. 重大阶段后的验证
- A 阶段完成后：`./build/simulator ./coremark.bin`
- B 阶段完成后：`./build/simulator ./coremark.bin`

### C3. 性能判定建议（轻量）
- 保留每阶段关键统计日志（至少 IPC、分支错误率、关键 predictor 覆盖率）。
- 与阶段起点做对比：接受小波动，关注是否出现持续性退化。

---

## 4. Git 存档与回退策略

1. 每个子阶段（A1/A2/A3/B1/B2）结束即提交一次。
2. 每个大阶段结束打 tag：
   - `bpu-tuning-A-stable`
   - `bpu-tuning-B-stable`
3. 如出现退化，优先 `git bisect` 在阶段内定位，不跨阶段混改。

---

## 5. 推荐执行顺序（本轮）

1. A1 更新策略修复  
2. A2 `use_alt_on` 升级  
3. A3 SC/Loop 归因修正  
4. 阶段 A 汇总验证（含 `coremark.bin`）  
5. B1 通路参数化（TN_MAX 适配）  
6. B2 历史体系扩展（拉到 TN_MAX=8）  
7. 阶段 B 汇总验证（含 `coremark.bin`）

