# Correctness Masking Review

Date: 2026-03-23
Scope: review whether current simulator contains logic that can mask DUT bugs by forcing REF/oracle/frontend/translation side to follow DUT, or by skipping checks too broadly.

## Findings

### 1. `difftest_skip()` explicitly copies DUT GPRs into REF
Severity: high

Evidence:
- [diff.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/diff.cpp#L273)
- [diff.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/diff.cpp#L277)

Behavior:
- `difftest_skip()` executes one REF step.
- It then overwrites `ref_cpu.state.gpr[i]` with `dut_cpu.gpr[i]`.
- This does not correct DUT, but it does realign REF to DUT and suppresses later register mismatches.

Assessment:
- Any path that reaches `difftest_skip()` is no longer a pure checker.
- `skip` must therefore remain extremely narrow and auditable.

Required follow-up:
- Inventory every current `skip` source.
- Reduce `skip` scope or replace it with a more precise check that does not bulk realign REF GPRs.

### 2. Store sideband lag currently turns a mismatch into `skip`
Severity: high

Evidence:
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L464)
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L483)
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L487)

Behavior:
- For committed stores, if `addr_valid && data_valid` is not ready on the STQ sideband, the code sets `*skip = true` and clears `dut_cpu.store`.
- Comment says this avoids aborting the run when store addr/data sideband lags ROB commit by a cycle.

Assessment:
- This is a masking path.
- It can hide real bugs in store commit / sideband timing / STQ bookkeeping.
- Because `skip` later copies DUT GPRs into REF, this is more than a local sideband suppression.

Required follow-up:
- Replace with a precise delayed-sideband handling mechanism or a stricter bounded replay/check.
- Do not leave this as a long-term correctness escape hatch.

### 3. `va2pa_fix()` is an ISA-tolerance path, not an immediate blocker
Severity: accepted with explicit scope

Evidence:
- [ref.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/ref.cpp#L211)
- [ref.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/ref.cpp#L227)
- [ref.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/ref.cpp#L1733)
- [ref.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/ref.cpp#L1753)
- [ref.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/ref.cpp#L1762)

Behavior:
- `difftest_step(true)` passes DUT page-fault bits into REF by `set_dut_page_fault_expect(...)`.
- `RefCpu::va2pa_fix()` runs real translation first.
- If DUT faults while REF translation succeeds, the REF side can still return fault.

Assessment:
- For the specific stale-page-table / pre-`sfence.vma` window, this is an ISA-permitted tolerance rather than an arbitrary correction.
- It does not modify DUT architectural state.
- Therefore this path should not currently be treated as a correctness blocker by itself.

Required follow-up:
- Keep its scope tightly limited to the architecturally permitted stale-translation window.
- Document that it is an allowed REF-side tolerance, not a general-purpose mismatch suppression mechanism.

### 4. Oracle frontend redirect path can snap oracle architectural state back to DUT
Severity: medium

Evidence:
- [br_oracle.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/br_oracle.cpp#L36)
- [br_oracle.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/br_oracle.cpp#L105)
- [br_oracle.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/diff/br_oracle.cpp#L111)

Behavior:
- On `refetch`, if oracle GPRs diverge from DUT, `sync_oracle_arch_state_from_dut()` copies DUT GPR/CSR/store/fault state into oracle.

Assessment:
- This does not correct DUT.
- But it does realign the oracle frontend model to DUT and can mask oracle-side detection of architectural drift after redirect/refetch.
- This is acceptable only as an oracle-frontend recovery mechanism if clearly scoped and documented, not as evidence that DUT remained correct.

Required follow-up:
- Audit whether this path is still required after current LLC/shared-topology fixes.
- If retained, document that oracle mode is not a strict architectural proof once this recovery triggers.

### 5. FAST/CKPT restore explicitly forces DUT/backend/frontend/MMIO state from REF snapshot
Severity: medium

Evidence:
- [BackTop.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/back-end/BackTop.cpp#L416)
- [main.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/main.cpp#L355)
- [main.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/main.cpp#L367)
- [main.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/main.cpp#L392)
- [main.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/main.cpp#L401)
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L611)
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L621)
- [rv_simu_mmu_v2.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/rv_simu_mmu_v2.cpp#L627)

Behavior:
- FAST/CKPT path restores backend architectural state from REF snapshot.
- Then it synchronizes MMIO device state from backing memory and forcibly resets frontend to restored PC.

Assessment:
- This is intentional bootstrap/restore logic, not a normal run-time masking bug.
- But it means FAST/CKPT correctness only proves behavior after the restore boundary, not before it.
- This distinction must stay explicit in PR text and validation claims.

Required follow-up:
- Keep as restore-only behavior.
- Do not treat FAST restore as evidence that DUT can self-recover from the same divergence without external synchronization.

### 6. Special timer MMIO loads are fully skipped in difftest
Severity: low to medium

Evidence:
- [RealDcache.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/MemSubSystem/RealDcache.cpp#L203)
- [RealDcache.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/MemSubSystem/RealDcache.cpp#L210)
- [RealDcache.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/MemSubSystem/RealDcache.cpp#L215)

Behavior:
- Loads from `0x1fd0e000/0x1fd0e004` return the synthetic timer value and mark `uop.difftest_skip = true`.

Assessment:
- This may be acceptable for side-effecting or model-specific timer semantics.
- But it should remain tightly scoped to only those addresses.
- Because `skip` is heavy-handed, this still deserves audit.

Required follow-up:
- Keep the skip width minimal.
- Consider a dedicated timer compare path that avoids full `skip` if feasible.

## Non-findings

### Coherent PTW fallback is not itself a masking path
Evidence:
- [TlbMmu.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/back-end/Lsu/TlbMmu.cpp#L224)
- [TlbMmu.cpp](/nfs_global/S/daiyihao/project/qm-rocky/dev/sw/new/simulator_to_merge/back-end/Lsu/TlbMmu.cpp#L333)

Assessment:
- `walk_and_refill_coherent()` reads PTEs via committed-store-coherent LSU visibility.
- This is modeling intended memory visibility, not forcing DUT to a correct answer.
- It should still be verified carefully, but it is not the same category as REF/oracle/skip realignment.

## Priority order to clean up later

1. Replace store-sideband `skip` with a precise non-masking check.
2. Audit all remaining `difftest_skip` sites and reduce them to the smallest possible scope.
3. Re-evaluate oracle refetch resync after mainline correctness is closed.
4. Keep FAST/CKPT restore logic documented as restore semantics, not correctness proof.
5. Keep `va2pa_fix()` explicitly scoped and documented as an ISA-tolerance path only.

## Current conclusion

The current tree does not appear to contain a normal-run path that silently patches DUT architectural state back to REF-correct values after a mismatch. However, it still contains several paths that can suppress, defer, or realign checking, so passing runs still need to be interpreted with that limitation in mind.
