# AXI Interconnect Integration

## Current active topology

The repository no longer uses the earlier split design where only icache talked
to AXI and the rest of the memory subsystem stayed on a legacy private path.

In the current top-level `SimCpu` integration:

- `DCache` read traffic is bridged onto `MASTER_DCACHE_R`
- `DCache` write-back traffic is bridged onto `MASTER_DCACHE_W`
- peripheral/MMIO traffic is bridged onto `MASTER_EXTRA_R/W`
- the real icache can use `MASTER_ICACHE` when
  `CONFIG_ICACHE_USE_AXI_MEM_PORT=1`

All of those masters connect to the same top-level AXI interconnect, router,
DDR model, and MMIO bus owned by `SimCpu`.

## What still exists but is not the active SoC path

`MemSubsystem` still contains an internal AXI runtime object. That code path is
kept for local integration support, but the top-level SoC flow disables it with
`mem_subsystem.set_internal_axi_runtime_active(false)`.

That means the authoritative Linux/SoC runs should be understood as:

- one top-level shared AXI fabric
- optional LLC inside that fabric
- no separate active `MemSubsystem` private AXI runtime

## LLC on/off semantics

`CONFIG_AXI_LLC_ENABLE` configures whether the top-level interconnect enables
its LLC.

- `CONFIG_AXI_LLC_ENABLE=1`: requests go through the shared fabric with LLC
- `CONFIG_AXI_LLC_ENABLE=0`: the same shared fabric stays in use, but the LLC
  is disabled and the system falls back to L1 I/D-cache only

This toggle does not swap DCache/PTW/peripheral over to a different interconnect.

## ICache path semantics

`CONFIG_ICACHE_USE_AXI_MEM_PORT` controls how the real icache gets miss data:

- `1`: use the top-level AXI `MASTER_ICACHE` port
- `0`: use the fixed-latency local read adapter in `ICacheTop`

For Linux matrix work, the recommended quadrant profiles keep this set to `1`
so the real icache stays on the same SoC fabric and only the LLC enable bit
changes between `llc0` and `llc1`.

When `CONFIG_BPU` is off, the frontend runs in Oracle mode and the real icache
model is not stepped. In that case the compiled icache AXI setting is present
for build consistency but not exercised at runtime.

## Initialization order

The shared-fabric initialization order is:

1. `mem_subsystem.init()`
2. `axi_interconnect.init()`
3. `axi_router.init()`
4. `axi_ddr.init()`
5. `axi_mmio.init()`
6. `front.init()`

This keeps the shared AXI fabric ready before the frontend starts issuing
requests.
