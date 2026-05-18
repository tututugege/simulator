# Targeted Training IO Widths (Large, Verified)

适用范围：

- 配置：`make large`
- BPU：开启（`CONFIG_BPU` enabled）
- 统计口径：按训练函数签名对应 `CombIn` / `CombOut` 的**完整逻辑位宽**统计，使用 `*_BITS` 语义，不使用 `sizeof(...) * 8`
- 范围：仅覆盖本次等价 IO 收窄涉及的目标训练函数

---

## 1. Large 配置常量

| 常量 | 值 |
|------|----|
| FETCH_WIDTH | 16 |
| COMMIT_WIDTH | 8 |
| TN_MAX | 4 |
| BPU_BANK_NUM | 16 |
| BPU_SCL_META_NTABLE | 8 |
| BPU_SCL_META_IDX_BITS | 16 |
| BPU_LOOP_META_IDX_BITS | 16 |
| BPU_LOOP_META_TAG_BITS | 16 |
| TAGE_IDX_WIDTH | 12 |
| TAGE_TAG_WIDTH | 8 |
| GHR_LENGTH | 512 |
| FH_N_MAX | 3 |
| RAS_DEPTH | 64 |

---

## 2. 目标训练函数位宽

| Function | File | Input Bits | Output Bits |
|----------|------|------------|-------------|
| `front_read_stage_input_comb` | `front-end/front_top.cpp` | 7 | 12 |
| `bpu_pre_read_req_comb` | `front-end/BPU/BPU.h` | 369 | 875 |
| `bpu_post_read_req_comb` | `front-end/BPU/BPU.h` | 7332 | 22509 |
| `bpu_submodule_bind_comb` | `front-end/BPU/BPU.h` | 1856 | 1680 |
| `bpu_predict_main_comb` | `front-end/BPU/BPU.h` | 5798 | 6502 |
| `bpu_hist_comb` | `front-end/BPU/BPU.h` | 6944 | 5935 |
| `bpu_queue_comb` | `front-end/BPU/BPU.h` | 3152 | 3281 |

---

## 3. 备注

- 本文件用于记录本次 PR 涉及函数在 `large` 配置下的核准位宽。
- 现有 [TRAINING_FUNCTION_LIST.md](/home/watts/simulator-main-mainline/front-end/TRAINING_FUNCTION_LIST.md) 仍保留旧冻结口径，未在本次 PR 中做全量 large 重算。
- 因此，本次 PR 只以本文件作为目标函数的 `large` 统计依据，避免把未完成全量复核的条目混入同一清单。
