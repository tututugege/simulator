# ITLB Flush And Refetch Decouple Design

## Problem

The current frontend icache path flushes the ITLB (`mmu_model`) whenever a
generic `refetch` happens.

That is overly conservative:

- branch redirect / replay / predecode refetch are fetch-control events
- `satp` update and `SFENCE.VMA` are translation-coherence events

Using the same signal for both makes Linux workloads pay a large ITLB/PTW
penalty.

## Existing backend signal

The backend already exposes a narrow `SFENCE_VMA`-commit signal:

- `rob_bcast->fence`

This signal is already used by the LSU-side DTLB/MMU path and can be reused for
the frontend ITLB path.

## Design goal

Split:

- `refetch`
  - fetch-control only
  - does **not** flush ITLB entries
  - may cancel only the current in-flight PTW / translation walk
- `itlb_flush`
  - translation-context invalidation
  - does flush ITLB

## Flush sources

The frontend ITLB should flush on:

1. reset
2. explicit `itlb_flush` from backend (`SFENCE.VMA`)
3. `satp` change observed by frontend

The frontend ITLB should **not** flush on:

1. branch misprediction redirect
2. predecode replay
3. generic frontend refetch

## Refetch side effect

Generic `refetch` may still need to cancel an in-flight translation walk.

So the frontend MMU model should support two operations:

1. `flush()`
   - drop cached ITLB entries
   - cancel in-flight walk
2. `cancel_pending_walk()`
   - cancel only the in-flight walk
   - keep cached ITLB entries intact

## Signal plumbing

Add a dedicated boolean signal:

- `Back_out.itlb_flush`
- `front_top_in.itlb_flush`
- `icache_in.itlb_flush`

Source:

- `Back_out.itlb_flush = rob->out.rob_bcast->fence`

## ICacheTop behavior

### True icache path

Track:

- `last_satp`
- `satp_seen`

Flush `mmu_model` only when:

- `reset`
- `itlb_flush`
- `satp` changed

Cancel pending walk when:

- `refetch`

### Simple/ideal path

Reuse existing `satp_seen/last_satp` tracking, but replace:

- `|| refetch`

with:

- `|| itlb_flush`

Also cancel pending walk on:

- `refetch`

## Expected result

This preserves correctness for:

- `SFENCE.VMA`
- `satp` changes

while removing excessive ITLB invalidation on ordinary frontend redirect/replay
traffic.
