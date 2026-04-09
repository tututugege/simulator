# Front-end Training Function List

适用范围：

- 目录：`front-end/`
- 范围：front-end 主路径，排除 `icache/` 子目录内部实现
- 不纳入范围：`FrontTop.cpp` / `FrontTop.h`

---

## 0. 冻结口径

- 本清单只列**待训练函数**（`*_comb`）。
- `*_calc`、`*_seq_read`、`*_seq_write` 不进入待训练函数清单。
- 对于小 `*_comb`：
  - 若已被上层待训练 `*_comb` 完整包含，且不存在独立未被包含的使用场景，则不单列；
  - 若存在独立语义路径、未被上层待训练 `*_comb` 完整包含，则必须列入。
- 清单条目必须与最终代码一致：函数名、文件路径、IO 类型名逐项对应。

---

## 1. 正式待训练函数（清单 comb）

| Function | Module | File | Input（代码真实类型） | Output（代码真实类型） |
| --- | --- | --- | --- | --- |
| `fetch_address_FIFO_comb` | `fetch_address_FIFO` | `front-end/fifo/fetch_address_FIFO.cpp` | `FetchAddrCombIn` | `FetchAddrCombOut` |
| `instruction_FIFO_comb` | `instruction_FIFO` | `front-end/fifo/instruction_FIFO.cpp` | `InstructionCombIn` | `InstructionCombOut` |
| `PTAB_comb` | `PTAB` | `front-end/fifo/PTAB.cpp` | `PtabCombIn` | `PtabCombOut` |
| `front2back_FIFO_comb` | `front2back_FIFO` | `front-end/fifo/front2bank_FIFO.cpp` | `Front2BackCombIn` | `Front2BackCombOut` |
| `predecode_comb` | `predecode` | `front-end/predecode.cpp` | `predecode_read_data` | `PredecodeResult` |
| `predecode_checker_comb` | `predecode_checker` | `front-end/predecode_checker.cpp` | `predecode_checker_read_data` | `predecode_checker_out` |
| `TypePredictor::pre_read_comb` | `TypePredictor` | `front-end/BPU/type_predictor/TypePredictor.h` | `InputPayload` | `PreReadCombOut` |
| `type_pred_comb` | `TypePredictor` | `front-end/BPU/type_predictor/TypePredictor.h` | `TypePredCombIn` | `TypePredCombOut` |
| `tage_pre_read_comb` | `TAGE_TOP` | `front-end/BPU/dir_predictor/TAGE_top.h` | `TagePreReadCombIn` | `TagePreReadCombOut` |
| `tage_comb` | `TAGE_TOP` | `front-end/BPU/dir_predictor/TAGE_top.h` | `TageCombIn` | `TageCombOut` |
| `btb_pre_read_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | `BtbPreReadCombIn` | `BtbPreReadCombOut` |
| `btb_post_read_req_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | `BtbPostReadReqCombIn` | `BtbPostReadReqCombOut` |
| `btb_comb` | `BTB_TOP` | `front-end/BPU/target_predictor/BTB_top.h` | `BtbCombIn` | `BtbCombOut` |
| `bpu_predict_main_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuPredictMainCombIn` | `BpuPredictMainCombOut` |
| `bpu_nlp_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuNlpCombIn` | `BpuNlpCombOut` |
| `bpu_hist_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuHistCombIn` | `BpuHistCombOut` |
| `bpu_queue_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuQueueCombIn` | `BpuQueueCombOut` |
| `bpu_pre_read_req_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuPreReadReqCombIn` | `BpuPreReadReqCombOut` |
| `bpu_post_read_req_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuPostReadReqCombIn` | `BpuPostReadReqCombOut` |
| `bpu_submodule_bind_comb` | `BPU_TOP` | `front-end/BPU/BPU.h` | `BpuSubmoduleBindCombIn` | `BpuSubmoduleBindCombOut` |
| `front_global_control_comb` | `front_top` | `front-end/front_top.cpp` | `FrontGlobalControlCombIn` | `FrontGlobalControlCombOut` |
| `front_read_enable_comb` | `front_top` | `front-end/front_top.cpp` | `FrontReadEnableCombIn` | `FrontReadEnableCombOut` |
| `front_read_stage_input_comb` | `front_top` | `front-end/front_top.cpp` | `FrontReadStageInputCombIn` | `FrontReadStageInputCombOut` |
| `front_bpu_control_comb` | `front_top` | `front-end/front_top.cpp` | `FrontBpuControlCombIn` | `FrontBpuControlCombOut` |
| `front_ptab_write_comb` | `front_top` | `front-end/front_top.cpp` | `FrontPtabWriteCombIn` | `FrontPtabWriteCombOut` |
| `front_checker_input_comb` | `front_top` | `front-end/front_top.cpp` | `FrontCheckerInputCombIn` | `FrontCheckerInputCombOut` |
| `front_front2back_write_comb` | `front_top` | `front-end/front_top.cpp` | `FrontFront2backWriteCombIn` | `FrontFront2backWriteCombOut` |
| `front_output_comb` | `front_top` | `front-end/front_top.cpp` | `FrontOutputCombIn` | `FrontOutputCombOut` |

---

## 2. 当前冻结结论

- 清单仅保留待训练函数，未包含 `*_calc` 与 `seq_read/seq_write`。
- 已补齐未被上层待训练 `*_comb` 包含、且具有独立语义路径的 pre/post-read 类组合函数：
  - `TypePredictor::pre_read_comb`
  - `tage_pre_read_comb`
  - `btb_pre_read_comb`
  - `btb_post_read_req_comb`
- 已补齐 BPU/top-level 中独立存在、未被上层 `*_comb` 包含的组合函数（`bpu_pre/post/bind` 与 `front_*_comb`）。
- 以下 `*_comb` 作为非清单 comb 保留（被上层清单 comb 完整包含，故不单列）：
  - `front_bpu_input_comb`（被 `front_bpu_control_comb` 调用）
- 以下 `*_comb` 当前在代码中未进入有效调用路径，按“未使用不纳入清单”口径不列入：
  - `btb_gen_index_pre_comb`
  - `btb_mem_read_pre_comb`
  - `btb_gen_index_post_comb`
- 清单中函数名与 IO 类型名已按当前代码逐项对齐。
