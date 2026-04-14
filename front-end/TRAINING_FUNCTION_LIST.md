# Front-end Training Function List

适用范围：

- 目录：`front-end/`
- 范围：front-end 主路径，排除 `icache/` 子目录内部实现
- 本文件按当前训练 IO 声明（`train_IO.h` / 各模块实际 struct）精确统计位宽

---

## 0. 冻结口径

- 本清单只列**待训练函数**（`*_comb`）。
- `*_calc`、`*_seq_read`、`*_seq_write` 不进入待训练函数清单。
- 位宽统计口径为：**按函数签名对应训练 IO struct 的完整逻辑位宽**精确计数，不做“只取实际访问字段”的裁剪。
- 若某函数仅处于调用链中、但其核心功能在当前永久关闭配置下整体失效，则按“当前实际未启用”处理，可移出清单。

---

## 1. 当前配置常量

| 常量 | 值 |
|------|----|
| FETCH_WIDTH | 4 |
| COMMIT_WIDTH | 2 |
| TN_MAX | 4 |
| BPU_BANK_NUM | 4 |
| BPU_SCL_META_NTABLE | 8 |
| BPU_SCL_META_IDX_BITS | 16 |
| BPU_LOOP_META_IDX_BITS | 16 |
| BPU_LOOP_META_TAG_BITS | 16 |
| TAGE_IDX_WIDTH | 6 |
| TAGE_TAG_WIDTH | 8 |
| GHR_LENGTH | 512 |
| FH_N_MAX | 3 |
| RAS_DEPTH | 16 |
| Q_DEPTH | 500 |
| FETCH_ADDR_FIFO_SIZE | 8 |
| INSTRUCTION_FIFO_SIZE | 8 |
| PTAB_SIZE | 8 |
| FRONT2BACK_FIFO_SIZE | 8 |

### 类型位宽参考

| 类型 | 位宽 |
|------|------|
| wire1_t | 1 |
| wire2_t | 2 |
| wire3_t | 3 |
| wire16_t | 16 |
| wire32_t | 32 |
| predecode_type_t | 2 |
| pcpn_t | 3 |
| br_type_t | 3 |
| tage_idx_t | 6 |
| tage_tag_t | 8 |
| tage_scl_meta_idx_t | 16 |
| tage_scl_meta_sum_t | 16 |
| tage_loop_meta_idx_t | 16 |
| tage_loop_meta_tag_t | 16 |
| tage_path_hist_t | 16 |
| nlp_index_t | 12 |
| nlp_tag_t | 30 |
| nlp_conf_t | 2 |

---

## 2. 正式待训练函数（当前冻结版）

| # | Function | Module | File | Input Bits | Output Bits |
|---|----------|--------|------|------------|-------------|
| 1 | `fetch_address_FIFO_comb` | `fetch_address_FIFO` | `front-end/fifo/fetch_address_FIFO.cpp` | 296 | 70 |
| 2 | `instruction_FIFO_comb` | `instruction_FIFO` | `front-end/fifo/instruction_FIFO.cpp` | 3896 | 870 |
| 3 | `PTAB_comb` | `PTAB` | `front-end/fifo/PTAB.cpp` | 10285 | 3431 |
| 4 | `front2back_FIFO_comb` | `front2back_FIFO` | `front-end/fifo/front2bank_FIFO.cpp` | 11492 | 2558 |
| 5 | `predecode_comb` | `predecode` | `front-end/predecode.cpp` | 64 | 34 |
| 6 | `predecode_checker_comb` | `predecode_checker` | `front-end/predecode_checker.cpp` | 204 | 37 |
| 7 | `TypePredictor::pre_read_comb` | `TypePredictor` | `front-end/BPU/type_predictor/TypePredictor.h` | 204 | 120 |
| 8 | `type_pred_comb` | `TypePredictor` | `front-end/BPU/type_predictor/TypePredictor.h` | 564 | 78 |
| 9 | `tage_pre_read_comb` | `TAGE_TOP` | `front-end/BPU/dir_predictor/TAGE_top.h` | 2449 | 495 |
| 10 | `tage_comb` | `TAGE_TOP` | `front-end/BPU/dir_predictor/TAGE_top.h` | 3189 | 1646 |
| 11 | `btb_pre_read_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | 105 | 183 |
| 12 | `btb_post_read_req_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | 1844 | 30 |
| 13 | `btb_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | 1844 | 989 |
| 14 | `bpu_predict_main_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 27448 | 1606 |
| 15 | `bpu_hist_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 26320 | 2859 |
| 16 | `bpu_queue_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 26296 | 769 |
| 17 | `bpu_pre_read_req_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 26296 | 324 |
| 18 | `bpu_post_read_req_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 26296 | 5565 |
| 19 | `bpu_submodule_bind_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | 31219 | 420 |
| 20 | `front_global_control_comb` | `front_top` | `front-end/front_top.cpp` | 67 | 34 |
| 21 | `front_read_enable_comb` | `front_top` | `front-end/front_top.cpp` | 9 | 6 |
| 22 | `front_read_stage_input_comb` | `front_top` | `front-end/front_top.cpp` | 7 | 2897 |
| 23 | `front_bpu_control_comb` | `front_top` | `front-end/front_top.cpp` | 699 | 1328 |
| 24 | `front_ptab_write_comb` | `front_top` | `front-end/front_top.cpp` | 1149 | 1145 |
| 25 | `front_checker_input_comb` | `front_top` | `front-end/front_top.cpp` | 1578 | 204 |
| 26 | `front_front2back_write_comb` | `front_top` | `front-end/front_top.cpp` | 1616 | 2559 |
| 27 | `front_output_comb` | `front_top` | `front-end/front_top.cpp` | 2559 | 1277 |

---

## 3. 删除结论

### 3.1 从清单删除

- `bpu_nlp_comb`

### 3.2 删除理由

- 当前配置中 `front-end/config/frontend_feature_config.h` 定义了 `FRONTEND_DISABLE_2AHEAD`，因此 `ENABLE_2AHEAD` 永远不会打开。
- `bpu_nlp_comb` 虽然仍在 `bpu_core_comb_calc` 调用链上，但其有效 NLP / 2-Ahead 逻辑整体位于 `#ifdef ENABLE_2AHEAD` 分支内。
- 在当前冻结配置下，它只承担“把相关输出清零”的语义，等价于一个当前项目策略下永久失效的功能壳。
- 按“实际未启用代码不纳入待训练函数清单”的口径，`bpu_nlp_comb` 应移出正式清单。

---

## 4. 非清单 comb / 死路径说明

### 4.1 被上层完整包含，不单列

- `front_bpu_input_comb`（被 `front_bpu_control_comb` 完整包裹）

### 4.2 当前未进入有效调用路径，不列入

- `btb_gen_index_pre_comb`
- `btb_mem_read_pre_comb`
- `btb_gen_index_post_comb`
- `PTABModel::peek_mini_flush`

### 4.3 在当前永久关闭配置下失效的 2-Ahead 相关机制

- BPU 的 NLP stage1 / stage2 预测逻辑
- PTAB 的 dummy entry 注入语义
- front-end 中 2-Ahead 额外 fetch-address 写入路径

---
