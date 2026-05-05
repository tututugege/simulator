#!/usr/bin/env python3

import os
import runpy
import sys


UPSTREAM_SCRIPT = (
    "/nfs_global/S/daiyihao/project/qm-rocky/run/260408/1/"
    "simulator/script/plot_perf_cache_hit.py"
)


def main() -> int:
    if not os.path.exists(UPSTREAM_SCRIPT):
        sys.stderr.write(f"upstream script not found: {UPSTREAM_SCRIPT}\n")
        return 1
    runpy.run_path(UPSTREAM_SCRIPT, run_name="__main__")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
