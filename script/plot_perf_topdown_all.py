#!/usr/bin/env python3
import argparse
import math
import os
import re
from typing import Dict, List, Optional

os.environ.setdefault("MKL_THREADING_LAYER", "GNU")
os.environ.setdefault("MPLBACKEND", "Agg")

import numpy as np

from plot_perf_topdown import compute_geomean_from_spec, plot_report


REGEX_ANSI = re.compile(r"\x1b\[[0-9;]*m")
REGEX_INST = re.compile(r"instruction\s+num:\s*(\d+)", re.IGNORECASE)
REGEX_CYC = re.compile(r"cycle\s+num:\s*(\d+)", re.IGNORECASE)


REF_TIMES = {
    "400.perlbench": 9770,
    "401.bzip2": 9650,
    "403.gcc": 8050,
    "429.mcf": 9120,
    "445.gobmk": 10490,
    "456.hmmer": 9270,
    "458.sjeng": 12100,
    "462.libquantum": 20700,
    "464.h264ref": 22100,
    "471.omnetpp": 6250,
    "473.astar": 7020,
    "483.xalancbmk": 6900,
}

INST_COUNTS = {
    "400.perlbench": 0,
    "401.bzip2": 2232007993519,
    "403.gcc": 1253567418409,
    "429.mcf": 293322610121,
    "445.gobmk": 2131355859079,
    "456.hmmer": 3616124850901,
    "458.sjeng": 2630677491559,
    "462.libquantum": 2229437961871,
    "464.h264ref": 5355011654218,
    "471.omnetpp": 1132608900700,
    "473.astar": 973000063337,
    "483.xalancbmk": 1061725890164,
}


def strip_ansi(text: str) -> str:
    return REGEX_ANSI.sub("", text)


def extract_last_int(text: str, label: str) -> Optional[int]:
    m = re.findall(rf"(?mi)^\s*(?:-\s*)?{re.escape(label)}\s*:\s*(-?\d+)\b", text)
    return int(m[-1]) if m else None


def extract_last_pct(text: str, label: str) -> Optional[float]:
    m = re.findall(
        rf"(?mi)^\s*(?:-\s*)?{re.escape(label)}\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*%",
        text,
    )
    return float(m[-1]) if m else None


def normalize_bench_key(bench_name: str) -> str:
    name = bench_name
    for suffix in ("_ref", "_base", "_peak", "_test"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    m = re.match(r"^(\d+\.[A-Za-z0-9]+)", name)
    if m:
        return m.group(1)
    if "_" in name:
        return name.split("_", 1)[0]
    return name


def parse_log(path: str) -> Optional[Dict[str, float]]:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        content = strip_ansi(f.read())

    if "Success!!!!" not in content:
        return None

    inst_matches = REGEX_INST.findall(content)
    cyc_matches = REGEX_CYC.findall(content)
    if not inst_matches or not cyc_matches:
        return None

    inst = int(inst_matches[-1])
    cyc = int(cyc_matches[-1])
    if inst <= 0 or cyc <= 0:
        return None

    return {
        "inst": float(inst),
        "cyc": float(cyc),
        "core_slots": float(extract_last_int(content, "Total Slots") or 0),
        "idu_slots": float(extract_last_int(content, "IDU Total Slots") or 0),
        "frontend_core": float(extract_last_pct(content, "Frontend Bound") or math.nan),
        "backend_core": float(extract_last_pct(content, "Backend Bound") or math.nan),
        "bad_spec_core": float(extract_last_pct(content, "Bad Speculation") or math.nan),
        "retiring_core": float(extract_last_pct(content, "Retiring") or math.nan),
        "frontend_idu": float(extract_last_pct(content, "IDU Frontend Bound") or math.nan),
        "backend_idu": float(extract_last_pct(content, "IDU Backend Bound") or math.nan),
        "bad_spec_idu": float(extract_last_pct(content, "IDU Bad Speculation") or math.nan),
        "retiring_idu": float(extract_last_pct(content, "IDU Retiring") or math.nan),
    }


def aggregate_pct(records: List[Dict[str, float]], value_key: str, slot_key: str) -> float:
    weighted_sum = 0.0
    weighted_slots = 0.0
    plain_vals: List[float] = []

    for record in records:
        value = record.get(value_key, math.nan)
        if not np.isfinite(value):
            continue
        slots = record.get(slot_key, 0.0)
        if np.isfinite(slots) and slots > 0:
            weighted_sum += value * slots
            weighted_slots += slots
        else:
            plain_vals.append(value)

    if weighted_slots > 0:
        return weighted_sum / weighted_slots
    if plain_vals:
        return float(sum(plain_vals) / len(plain_vals))
    return math.nan


def summarize_benchmark(bench_dir: str, cpu_ghz: float) -> Optional[Dict[str, float]]:
    log_paths = sorted(
        os.path.join(bench_dir, f) for f in os.listdir(bench_dir) if f.endswith(".log")
    )
    if not log_paths:
        return None

    parsed = []
    for log_path in log_paths:
        rec = parse_log(log_path)
        if rec:
            parsed.append(rec)

    if not parsed:
        return None

    inst_sum = sum(r["inst"] for r in parsed)
    cyc_sum = sum(r["cyc"] for r in parsed)
    if inst_sum <= 0 or cyc_sum <= 0:
        return None

    cpi = cyc_sum / inst_sum
    ipc = inst_sum / cyc_sum

    bench_name = os.path.basename(bench_dir)
    bench_key = normalize_bench_key(bench_name)
    ref_time = REF_TIMES.get(bench_key, 0)
    total_insts = INST_COUNTS.get(bench_key, 0)
    spec = math.nan
    if ref_time > 0 and total_insts > 0 and cpi > 0 and cpu_ghz > 0:
        pred_cycles = total_insts * cpi
        pred_time = pred_cycles / (cpu_ghz * 1e9)
        if pred_time > 0:
            spec = ref_time / pred_time

    retiring_core = aggregate_pct(parsed, "retiring_core", "core_slots") / 100.0
    bad_spec_core = aggregate_pct(parsed, "bad_spec_core", "core_slots") / 100.0
    frontend_core = aggregate_pct(parsed, "frontend_core", "core_slots") / 100.0
    backend_core = aggregate_pct(parsed, "backend_core", "core_slots") / 100.0

    retiring_idu = aggregate_pct(parsed, "retiring_idu", "idu_slots") / 100.0
    bad_spec_idu = aggregate_pct(parsed, "bad_spec_idu", "idu_slots") / 100.0
    frontend_idu = aggregate_pct(parsed, "frontend_idu", "idu_slots") / 100.0
    backend_idu = aggregate_pct(parsed, "backend_idu", "idu_slots") / 100.0

    use_idu = any(np.isfinite(v) for v in [retiring_idu, bad_spec_idu, frontend_idu, backend_idu])
    if use_idu:
        retiring = retiring_idu
        bad_spec = bad_spec_idu
        frontend = frontend_idu
        backend = backend_idu
    else:
        retiring = retiring_core
        bad_spec = bad_spec_core
        frontend = frontend_core
        backend = backend_core

    return {
        "bench": bench_name,
        "ipc": ipc,
        "retiring": retiring,
        "bad_spec": bad_spec,
        "frontend": frontend,
        "backend": backend,
        "retiring_core": retiring_core,
        "bad_spec_core": bad_spec_core,
        "frontend_core": frontend_core,
        "backend_core": backend_core,
        "retiring_idu": retiring_idu,
        "bad_spec_idu": bad_spec_idu,
        "frontend_idu": frontend_idu,
        "backend_idu": backend_idu,
        "spec": spec,
        "valid_logs": float(len(parsed)),
        "total_logs": float(len(log_paths)),
    }


def find_config_dirs(root: str) -> List[str]:
    config_dirs = []
    for name in sorted(os.listdir(root)):
        path = os.path.join(root, name)
        if not os.path.isdir(path):
            continue
        for child in os.listdir(path):
            child_path = os.path.join(path, child)
            if os.path.isdir(child_path) and any(p.endswith(".log") for p in os.listdir(child_path)):
                config_dirs.append(path)
                break
    return config_dirs


def summarize_config(config_dir: str, cpu_ghz: float) -> List[Dict[str, float]]:
    benches: List[Dict[str, float]] = []
    for name in sorted(os.listdir(config_dir)):
        bench_dir = os.path.join(config_dir, name)
        if not os.path.isdir(bench_dir):
            continue
        summary = summarize_benchmark(bench_dir, cpu_ghz)
        if summary is None:
            continue
        benches.append(summary)
    return benches


def output_path_for_config(config_dir: str, output_name: str, output_dir: Optional[str]) -> str:
    config_name = os.path.basename(config_dir.rstrip(os.sep))
    if output_dir:
        stem, ext = os.path.splitext(output_name)
        ext = ext or ".png"
        os.makedirs(output_dir, exist_ok=True)
        return os.path.join(output_dir, f"{config_name}_{stem}{ext}")
    return os.path.join(config_dir, output_name)


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Batch plot Top-Down/IPC/SPEC figures for all config folders under a root directory."
        )
    )
    parser.add_argument(
        "--root",
        default=os.path.dirname(os.path.abspath(__file__)),
        help="root directory that contains config subfolders (default: script directory)",
    )
    parser.add_argument(
        "--output-name",
        default="perf_topdown_spec.png",
        help="output figure name when writing inside each config folder",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="optional shared output directory (filename will be prefixed by config name)",
    )
    parser.add_argument(
        "--cpu-ghz",
        type=float,
        default=1.0,
        help="CPU frequency in GHz for SPEC ratio estimation (default: 1.0)",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    config_dirs = find_config_dirs(root)
    if not config_dirs:
        raise RuntimeError(f"No valid config directories found under: {root}")

    print(f"[INFO] root: {root}")
    print(f"[INFO] found {len(config_dirs)} config directories")

    success_count = 0
    for config_dir in config_dirs:
        benches = summarize_config(config_dir, args.cpu_ghz)
        if not benches:
            print(f"[WARN] skip {config_dir}: no valid benchmark data")
            continue

        geomean_spec = compute_geomean_from_spec(benches)
        out_png = output_path_for_config(config_dir, args.output_name, args.output_dir)

        print(
            f"[INFO] plotting {os.path.basename(config_dir)}: "
            f"{len(benches)} benches -> {out_png}"
        )
        plot_report(benches, geomean_spec, out_png)
        success_count += 1

    if success_count == 0:
        raise RuntimeError("No figure was generated. Please check input logs.")
    print(f"[INFO] done. generated {success_count} figures.")


if __name__ == "__main__":
    main()
