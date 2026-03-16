# Perf/Debug Decouple Plan (diag remains hardware)

## 1. Goal and constraints
- Keep `diag_val` on the hardware data path.
- Decouple `perf` and `debug` concerns from hardware interface structs.
- Refactor in small steps with compile-stable transitions.
- No functional behavior change.

## 2. Scope
- Backend type definitions and context:
  - `back-end/include/types.h`
  - `back-end/include/PerfCount.h`
- Modules with direct debug/perf touching on hot path (migrate gradually):
  - `back-end/{Idu,Ren,Dispatch,Isu,Exu,Rob,...}`

Out of scope for phase-1:
- Renaming `diag_val`
- Re-architecting all logs/macros
- Changing perf formulas
- Any `front-end` file changes

## 3. Current pain points
- `InstInfo/MicroOp` mix hardware and debug metadata (e.g. `instruction`, `inst_idx`, `cplt_time`, etc.).
- Perf counters are globally reachable; update points are scattered.

## 4. Target structure

### 4.1 Hardware path (must stay in main structs)
- Keep hardware-valid fields in `InstInfo/MicroOp` and stage IO structs.
- `diag_val` stays in hardware structs.
- `flush_pipe` stays in hardware structs.
- `cplt_time` stays in hardware structs for now.

### 4.2 Debug sideband
- Introduce dedicated debug metadata container(s), for example:
  - `InstDebugMeta`
  - `UopDebugMeta`
- Debug fields move here first:
  - `instruction`
  - `difftest_skip`
  - `inst_idx`

### 4.3 Perf access boundary
- Keep storage in `SimContext::perf`.
- Add a light wrapper entry (e.g. helper/hook methods) to centralize updates.
- Modules should prefer wrapper calls over direct arbitrary field writes.
- Perf-related metadata may still travel across module boundaries temporarily, but
  should be treated as perf-side sideband rather than hardware functionality.

### 4.4 Confirmed perf-side metadata candidates
- `is_cache_miss` in `InstInfo/MicroOp`
  - Current role: classify memory stall as `mem_l1_bound` vs `mem_ext_bound`.
  - Not required for ISA-visible correctness.
- `LsuRobIO::miss_mask`
  - Current role: carry per-ROB miss state for ROB/Dispatch TMA classification.
  - Not required for correctness.
- `RobDisIO::head_is_memory`
  - Current role: tell Dispatch the oldest blocker is memory-related for TMA.
  - Not required for correctness.
- `RobDisIO::head_is_miss`
  - Current role: distinguish external-memory miss from L1-bound stall for TMA.
  - Not required for correctness.
- `RobDisIO::head_not_ready`
  - Current role: qualify the blocker classification used by Dispatch perf logic.
  - Not required for correctness.

### 4.5 IO audit result
- Keep as hardware/functional IO:
  - `committed_store_pending`
  - `IssDisIO::ready_num`
  - `RobDisIO::stall`
  - `LsuDisIO::{stq_free, ldq_free, ldq_alloc_idx, stq_tail, stq_tail_flag}`
  - `WbArbiterDcacheIO::{stall_ld, stall_st}`
  - `WritebufferDcacheIO::stall`
- Treat as perf-side candidates:
  - `LsuRobIO::miss_mask`
  - `RobDisIO::{head_is_memory, head_is_miss, head_not_ready}`
- Do not move yet:
  - `committed_store_pending` is used by ROB to block `SFENCE.VMA` commit, so it
    is functional rather than perf-only.

## 5. Step-by-step implementation

### Step 0: Baseline freeze
- Build and run current smoke flow once.
- Save baseline outputs/log snippets used for comparison.

Exit criteria:
- Current branch is reproducible before refactor.

### Step 1: Introduce debug sideband types (no behavior change)
- Add debug meta structs in `types.h` (or a new `debug_meta.h` included by `types.h`).
- Do not remove old fields yet.
- Add conversion helpers between old mixed structs and new sideband representation.

Exit criteria:
- Build passes.
- No call-site behavior change.

### Step 2: Backend debug field migration (gradual)
- Migrate one module at a time to read/write debug sideband instead of mixed fields.
- Suggested order:
  1. `Idu`
  2. `Dispatch`
  3. `Exu`
  4. `Rob`
- Keep compatibility adapters until all users migrate.

Exit criteria:
- Each module migration compiles and passes smoke before moving on.

### Step 3: Perf update boundary
- Keep `PerfCount` fields unchanged.
- Add wrapper/hook API for frequently updated counters.
- Move hot-path direct writes to wrappers incrementally.
- Start by isolating perf-side metadata flow:
  - `is_cache_miss`
  - `miss_mask`
  - `head_is_memory`
  - `head_is_miss`
  - `head_not_ready`

Exit criteria:
- Perf numbers match baseline within expected run variance.
- No direct writes in newly touched modules except through wrappers.

### Step 4: Remove temporary compatibility fields
- After all call sites migrate, delete old duplicated debug fields from hardware structs.
- Keep `diag_val` untouched.

Exit criteria:
- Full build pass.
- No stale adapter usage.

### Step 5: Cleanup and docs
- Update related docs with explicit HW/DEBUG/PERF boundaries.
- Add short contribution rule: new interface fields must be tagged as HW or DEBUG.

Exit criteria:
- Docs and code are aligned.

## 6. Validation checklist per step
- `make -j` build success.
- No new warnings from changed files.
- Smoke tests execute successfully.
- Diff of critical output/perf summary checked.

## 7. Risk and rollback
- Main risk: hidden dependency on debug fields in functional decisions.
- Mitigation: keep adapters during migration and switch module-by-module.
- Rollback: each step should be independently reversible without touching unrelated modules.

## 8. First concrete execution batch
1. Add debug sideband structs and adapters.
2. Migrate `Idu` debug reads/writes to sideband.
3. Migrate `Dispatch/Exu/Rob` debug reads/writes to sideband (one-by-one).
4. Add perf wrapper entry points and migrate touched hot paths.
5. Build + smoke check.
