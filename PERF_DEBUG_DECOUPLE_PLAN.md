# cplt_time Refactor Plan

## 0. Status
- Completed.
- `cplt_time` has been removed from shared `MicroOp` interface semantics.
- LSU now keeps request phase and ready timing locally.
- FU now keeps completion timing locally.
- Regression check passed with:
  - `instruction num = 3325303`
  - `cycle num = 1014161`
  - `ipc = 3.278871`

## 1. Goal
- Remove `cplt_time` from shared `MicroOp` interface semantics.
- Keep simulator timing behavior unchanged.
- Make completion-time bookkeeping local to the execution structure that owns it.
- Preserve hardware-simulator coding style: flat data, explicit state, no extra abstraction layers.

## 2. Original situation
- `cplt_time` was stored in `MicroOp`.
- In `AbstractFU` / `FixedLatencyFU` / `IterativeFU`, it was used as an internal completion timestamp.
- In `SimpleLsu`, it was used for two different meanings:
  - real completion cycle
  - internal state markers such as `REQ_WAIT_SEND`, `REQ_WAIT_RESP`, `REQ_WAIT_RETRY`, `REQ_WAIT_EXEC`
- That meant the field was not really a hardware-visible uop field. It was simulator scheduling state leaking into shared interfaces.
- A direct FU-side extraction was tried first and changed timing behavior under `dhrystone.bin`.
- Final conclusion: `cplt_time` had to be removed in dependency order, not by mechanical substitution.

## 3. Refactor direction

### 3.1 LSU side
- `SimpleLsu` currently uses `cplt_time` as mixed scheduler state; this should be split explicitly.
- Replace it with local LSU entry state, for example:
  - `enum class LoadState { WAIT_EXEC, WAIT_SEND, WAIT_RESP, WAIT_RETRY, READY }`
  - `int64_t ready_cycle`
- Rule:
  - state controls what phase the request is in
  - `ready_cycle` is used only when state depends on time
- Avoid magic sentinel integers once the split is done.

### 3.2 FU side
- Move completion-time tracking into each FU's internal queue/state.
- `MicroOp` stays as payload only.
- Example direction:
  - `FixedLatencyFU` pipeline stores `{uop, done_cycle}`
  - `IterativeFU` stores `{current_uop, done_cycle}` or `{busy, remaining_cycles}`
- External interface remains:
  - `accept(uop)`
  - `get_finished_uop()`
- Important constraint:
  - do this only after LSU no longer depends on incoming `uop.cplt_time`
  - preserve the exact current completion cycle semantics when converting

### 3.3 Shared interface boundary
- `MicroOp` should no longer contain `cplt_time` after migration is complete.
- If some module still needs internal timing, that timing must live in the owner structure, not the shared payload.

## 4. Scope
- In scope:
  - `back-end/include/types.h`
  - `back-end/Exu/include/AbstractFU.h`
  - `back-end/Exu/include/Fu.h`
  - `back-end/Exu/include/FPU.h`
  - `back-end/Exu/Exu.cpp`
  - `back-end/Lsu/SimpleLsu.cpp`
  - related local entry/state definitions
- Out of scope:
  - changing architectural behavior
  - changing throughput/latency model
  - changing `dbg` / `tma` structure naming

## 5. Execution record

### Step 0: Baseline freeze
- Rebuild and run `./build/simulator ./dhrystone.bin`.
- Record:
  - success/fail
  - instruction count
  - cycle count
  - IPC

Exit criteria:
- Current behavior is reproducible before touching `cplt_time`.

Result:
- Done.
- Baseline confirmed with `3325303 / 1014161 / 3.278871`.

### Step 1: LSU state split
- Refactor `SimpleLsu.cpp` first.
- Add explicit LSU-local scheduling state to LDQ/STQ entries.
- Replace the mixed `cplt_time + sentinel` scheme with:
  - phase/state field
  - optional `ready_cycle`
- Keep the logic flat and local to `SimpleLsu.cpp`; avoid introducing class hierarchies.
- During this step, keep `MicroOp::cplt_time` only as a compatibility field if some LSU output path still mirrors it.

Exit criteria:
- `SimpleLsu.cpp` no longer relies on magic `REQ_WAIT_*` values stored in `uop.cplt_time`.
- Build passes.
- `dhrystone.bin` result unchanged.

Result:
- Done.
- `LdqEntry` now keeps `LoadState + ready_cycle`.
- `REQ_WAIT_*`, `sent`, and `waiting_resp` were removed from `SimpleLsu`.

### Step 2: FU-local timing state
- Refactor `AbstractFU` hierarchy after LSU is decoupled.
- Introduce explicit internal pipeline entries, for example:
  - `struct FuPipeEntry { MicroOp uop; int64_t done_cycle; };`
- Replace FU-internal uses of `uop.cplt_time` with local `done_cycle`.
- Do not change external FU interfaces.
- Validate carefully for off-by-one timing behavior.

Exit criteria:
- No FU code reads/writes `uop.cplt_time`.
- Build passes.
- `dhrystone.bin` result unchanged.

Result:
- Done.
- `AbstractFU` now keeps local `done_cycle` state.
- Shared `MicroOp` payload is no longer used for FU timing.

### Step 3: Remove shared-field dependency
- After FU and LSU are both migrated, remove `cplt_time` from `MicroOp`.
- Fix remaining construction/copy sites.

Exit criteria:
- `rg "cplt_time" back-end` only shows local FU/LSU state names or comments, not `MicroOp::cplt_time`.
- Build passes.
- `dhrystone.bin` result unchanged.

Result:
- Done.
- `MicroOp::cplt_time` was removed from `types.h`.
- Code search no longer finds `cplt_time` in backend code.

### Step 4: Cleanup
- Rename any temporary local state names if needed.
- Update comments to state clearly:
  - completion timing is simulator-local execution state
  - it is not a shared hardware interface field

Exit criteria:
- Code and comments agree on ownership of timing state.

Result:
- Mostly done.
- The remaining work is only optional comment cleanup if needed later.

## 6. Actual implementation order
1. `SimpleLsu.cpp`
2. local LSU entry/state definitions
3. `AbstractFU.h`
4. `types.h` cleanup

## 7. Risks
- LSU refactor risk:
  - conflating "ready now" with "waiting on response"
  - losing special retry behavior
- FU refactor risk:
  - off-by-one timing change (`sim_time`, `latency`, writeback cycle)
  - hidden compatibility dependency from downstream modules that still observe `uop.cplt_time`
- Main mitigation:
  - decouple LSU first, then FU
  - rerun `dhrystone.bin` after each step

Observed outcome:
- This mitigation worked.
- After LSU was decoupled first, the FU-local timing migration no longer changed benchmark results.

## 8. Acceptance criteria
- `cplt_time` is no longer part of shared uop interface semantics.
- FU completion timing is private to FU internals.
- LSU request scheduling state is private to LSU internals.
- `./build/simulator ./dhrystone.bin` still reports:
  - `Success!!!!`
  - instruction num `3325303`
  - cycle num `1014161`
  - ipc `3.278871`
