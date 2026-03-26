# profile-switch coremark v2 summary

- Branch: `bpu-phaseA-robust-work`
- Base target: phaseA-robust (`TN_MAX=4`) with robustness checks kept
- Date: 2026-03-24

## What changed

1. Added profile switching in `Makefile`:
   - default profile: `large`
   - targets: `make small`, `make medium`, `make large`
   - profile sync: copy `frontend_feature_config.h.<profile>` and `config.h.<profile>` to active paths before build
2. Added `USE_AXI_KIT ?= 0` in `Makefile` to avoid compiling `axi-interconnect-kit` sources by default.
3. Kept phaseA constraint `TN_MAX == 4` and converted key ENTRY/INDEX checks to `static_assert` in `front-end/BPU/BPU_configs.h`.
4. Changed TAGE index width to auto-derive from entry count (`clog2`) in frontend profile configs.
5. Set `CONFIG_ICACHE_USE_AXI_MEM_PORT` default to 0 in profile `config.h.*` and active `include/config.h`.

## Commands used

For each profile:

- `make clean`
- `make -j8 <profile>`
- `./build/simulator ./coremark.bin`

## Results (v2)

| profile | make wall(s) | coremark wall(s) | sim_time | ipc |
|---|---:|---:|---:|---:|
| small  | 10.38 | 120.49 | 9,425,397 | 1.503299 |
| medium | 10.67 | 271.78 | 7,020,882 | 2.018149 |
| large  | 10.27 | 450.15 | 6,723,616 | 2.107376 |

## Raw log files (v2)

- `doc/tuning_logs/profile_small_make_v2.log`
- `doc/tuning_logs/profile_small_coremark_v2.log`
- `doc/tuning_logs/profile_medium_make_v2.log`
- `doc/tuning_logs/profile_medium_coremark_v2.log`
- `doc/tuning_logs/profile_large_make_v2.log`
- `doc/tuning_logs/profile_large_coremark_v2.log`

Snapshot configs saved during runs:

- `doc/tuning_logs/profile_small_active_config_v2.h`
- `doc/tuning_logs/profile_small_active_frontend_feature_config_v2.h`
- `doc/tuning_logs/profile_medium_active_config_v2.h`
- `doc/tuning_logs/profile_medium_active_frontend_feature_config_v2.h`
- `doc/tuning_logs/profile_large_active_config_v2.h`
- `doc/tuning_logs/profile_large_active_frontend_feature_config_v2.h`
