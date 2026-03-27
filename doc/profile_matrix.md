# Profile Matrix And Active Config Rules

## Verified behavior

`Makefile` selects a profile through `PROFILE`, then `profile-config` copies:

- `include/config.h.<PROFILE>` -> `include/config.h`
- `front-end/config/frontend_feature_config.h.<PROFILE>` ->
  `front-end/config/frontend_feature_config.h`

The copied files are active build inputs. They are not the long-term source of
truth. Editing the active copies directly is fragile because the next build may
overwrite them.

## Built-in profiles in this repository

| profile | `CONFIG_BPU` | `CONFIG_AXI_LLC_ENABLE` | `CONFIG_ICACHE_USE_AXI_MEM_PORT` | main intent |
|---|---:|---:|---:|---|
| `default` | 1 | 1 | 1 | real BPU, shared AXI fabric with LLC |
| `small` | 0 | 0 | 0 | Oracle frontend, reduced frontend/backend resources |
| `medium` | 0 | 0 | 0 | Oracle frontend, medium-sized resources |
| `large` | 0 | 0 | 0 | Oracle frontend, large resources matching `default` widths |

The built-in set does not cover all four `BPU on/off x LLC on/off` combinations.
Only `default` reaches `bpu1_llc1`; `small/medium/large` all stay in
`bpu0_llc0`.

## Four-quadrant profiles

To keep Linux matrix runs explicit and reproducible, the repository also carries
these paired profiles:

- `bpu0_llc0`
- `bpu0_llc1`
- `bpu1_llc0`
- `bpu1_llc1`

These quadrant profiles intentionally keep the frontend width/resource shape
aligned with `default`/`large` so the matrix changes only the intended feature
switches. They also keep `CONFIG_ICACHE_USE_AXI_MEM_PORT=1` in the quadrant
profiles so the real icache path can stay on the same top-level AXI fabric; the
only cache-hierarchy toggle between `llc0` and `llc1` is
`CONFIG_AXI_LLC_ENABLE`.

For `bpu0_*`, the frontend runs in Oracle mode because `CONFIG_BPU` is not
defined. In that mode the real icache model is not stepped even though the
quadrant profile keeps the shared-AXI compile path available.

## Recommended usage

Build one variant per build directory:

```bash
make PROFILE=bpu0_llc0 BUILD_DIR=build_bpu0_llc0
make PROFILE=bpu0_llc1 BUILD_DIR=build_bpu0_llc1
make PROFILE=bpu1_llc0 BUILD_DIR=build_bpu1_llc0
make PROFILE=bpu1_llc1 BUILD_DIR=build_bpu1_llc1
```

For quick target names, the Makefile also provides:

```bash
make bpu0_llc0 BUILD_DIR=build_bpu0_llc0
make bpu0_llc1 BUILD_DIR=build_bpu0_llc1
make bpu1_llc0 BUILD_DIR=build_bpu1_llc0
make bpu1_llc1 BUILD_DIR=build_bpu1_llc1
```

## Topology facts validated against current code

- `DCache`, PTW traffic, and peripheral MMIO all drive the top-level AXI
  interconnect owned by `SimCpu`.
- `MemSubsystem` still contains an internal AXI runtime implementation, but the
  top-level SoC path disables it through
  `mem_subsystem.set_internal_axi_runtime_active(false)`.
- With `CONFIG_ICACHE_USE_AXI_MEM_PORT=1`, the real icache uses the same
  top-level AXI read master as the rest of the SoC fabric.
- `CONFIG_AXI_LLC_ENABLE` only toggles the LLC inside that top-level fabric; it
  does not select a different DCache/PTW/peripheral interconnect.
