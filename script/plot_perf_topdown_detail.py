#!/usr/bin/env python3
import argparse
import math
import os
from typing import Dict, List, Optional, Tuple

import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter
import numpy as np

from plot_perf_topdown import (
    augment_config_meta_with_runtime,
    compute_geomean_from_spec,
    draw_microarch_config_panel,
    parse_num,
    parse_num_after_colon,
    parse_pct,
)


STACK_COMPONENTS: List[Tuple[str, str, str]] = [
    ("retiring", "Retiring", "#92b558"),
    ("bad_spec_other", "BadSpec Other", "#c0504d"),
    ("squash_waste", "Squash Waste", "#e08282"),
    ("fetch_latency", "Fetch Latency", "#b58ad8"),
    ("fetch_bw", "Fetch BW", "#7030a0"),
    ("frontend_other", "Frontend Other", "#d8c0ea"),
    ("l1_bound", "L1 Bound", "#f4b400"),
    ("ext_mem_bound", "Ext Mem Bound", "#f7c948"),
    ("memory_other", "Memory Other", "#ffe08a"),
    ("core_bound", "Core Bound", "#c99a00"),
    ("backend_other", "Backend Other", "#8f7300"),
]


def parse_perf_report(path: str):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = [x.strip() for x in f.readlines()]

    benches: List[Dict[str, float]] = []
    geomean_spec = None
    config_meta: Dict[str, str] = {}

    cur = None
    in_config_snapshot = False
    in_tma = False
    for line in lines:
        if not line:
            continue

        if line.startswith("LOG_ROOT_DIR ="):
            config_meta["LOG_ROOT_DIR"] = line.split("=", 1)[1].strip()
            continue
        if line == "CONFIG_SNAPSHOT_BEGIN":
            in_config_snapshot = True
            continue
        if line == "CONFIG_SNAPSHOT_END":
            in_config_snapshot = False
            continue
        if in_config_snapshot:
            if "=" in line:
                k, v = line.split("=", 1)
                config_meta[k.strip()] = v.strip()
            continue

        if line.startswith("Benchmark:"):
            if cur:
                benches.append(cur)
            bench_label = line.split("Benchmark:", 1)[1].strip()
            bench_label = bench_label.split(" (key=", 1)[0].strip()
            cur = {
                "bench": bench_label,
                "ipc": math.nan,
                "spec": math.nan,
                "frontend_bound": math.nan,
                "fetch_latency": math.nan,
                "fetch_bw": math.nan,
                "backend_bound": math.nan,
                "memory_bound": math.nan,
                "l1_bound": math.nan,
                "ext_mem_bound": math.nan,
                "core_bound": math.nan,
                "bad_speculation": math.nan,
                "squash_waste": math.nan,
                "retiring": math.nan,
            }
            in_tma = False
            continue

        if line.startswith("Final Estimated SPECint2006:"):
            geomean_spec = parse_num_after_colon(line)
            continue

        if cur is None:
            continue

        if line.startswith("Weighted IPC:"):
            value = parse_num(line)
            if value is not None:
                cur["ipc"] = value
            continue
        if line.startswith("SPEC Ratio:"):
            value = parse_num(line)
            if value is not None:
                cur["spec"] = value
            continue
        if line.startswith("TMA (Weighted):"):
            in_tma = True
            continue
        if line.startswith("TMA-IDU (Weighted):"):
            in_tma = False
            continue
        if not in_tma:
            continue

        tma_labels = {
            "Frontend Bound:": "frontend_bound",
            "Fetch Latency:": "fetch_latency",
            "Fetch BW:": "fetch_bw",
            "Backend Bound:": "backend_bound",
            "Memory Bound:": "memory_bound",
            "L1 Bound:": "l1_bound",
            "Ext Mem Bound:": "ext_mem_bound",
            "Core Bound:": "core_bound",
            "Bad Speculation:": "bad_speculation",
            "Squash Waste:": "squash_waste",
            "Retiring:": "retiring",
        }
        for label, key in tma_labels.items():
            if line.startswith(label):
                value = parse_pct(line)
                if value is not None:
                    cur[key] = value / 100.0
                break

    if cur:
        benches.append(cur)

    valid = [
        b
        for b in benches
        if any(
            np.isfinite(b[k])
            for k in (
                "frontend_bound",
                "fetch_latency",
                "fetch_bw",
                "backend_bound",
                "memory_bound",
                "l1_bound",
                "ext_mem_bound",
                "core_bound",
                "bad_speculation",
                "squash_waste",
                "retiring",
            )
        )
    ]
    return valid, geomean_spec, augment_config_meta_with_runtime(config_meta)


def _positive(value: float) -> float:
    return float(value) if np.isfinite(value) and value > 0.0 else 0.0


def _residual(parent: float, children: List[float]) -> float:
    return max(_positive(parent) - sum(children), 0.0)


def _stack_matrix(benches: List[Dict[str, float]]) -> np.ndarray:
    raw = np.zeros((len(STACK_COMPONENTS), len(benches)), dtype=float)
    for idx, b in enumerate(benches):
        fetch_latency = _positive(b.get("fetch_latency", math.nan))
        fetch_bw = _positive(b.get("fetch_bw", math.nan))
        frontend_other = _residual(b.get("frontend_bound", math.nan), [fetch_latency, fetch_bw])

        l1_bound = _positive(b.get("l1_bound", math.nan))
        ext_mem_bound = _positive(b.get("ext_mem_bound", math.nan))
        memory_other = _residual(b.get("memory_bound", math.nan), [l1_bound, ext_mem_bound])
        core_bound = _positive(b.get("core_bound", math.nan))
        backend_other = _residual(
            b.get("backend_bound", math.nan),
            [l1_bound, ext_mem_bound, memory_other, core_bound],
        )

        squash_waste = _positive(b.get("squash_waste", math.nan))
        bad_spec_other = _residual(b.get("bad_speculation", math.nan), [squash_waste])
        retiring = _positive(b.get("retiring", math.nan))

        raw[:, idx] = [
            retiring,
            bad_spec_other,
            squash_waste,
            fetch_latency,
            fetch_bw,
            frontend_other,
            l1_bound,
            ext_mem_bound,
            memory_other,
            core_bound,
            backend_other,
        ]

    totals = raw.sum(axis=0)
    normalized = np.zeros_like(raw)
    valid = totals > 0.0
    normalized[:, valid] = raw[:, valid] / totals[valid]
    return normalized


def plot_detail(
    benches: List[Dict[str, float]],
    geomean_spec: Optional[float],
    out_png: str,
    config_meta: Optional[Dict[str, str]] = None,
):
    if not benches:
        raise RuntimeError("No detailed TMA data parsed from perf report.")

    labels = [b["bench"] for b in benches]
    x = np.arange(len(labels), dtype=float)
    fig = plt.figure(figsize=(max(14, len(labels) * 0.76), 13.6), dpi=140)
    gs = fig.add_gridspec(2, 1, height_ratios=[4.0, 2.55], hspace=0.43)

    ax = fig.add_subplot(gs[0, 0])
    matrix = _stack_matrix(benches)
    bottom = np.zeros(len(labels), dtype=float)
    handles = []
    legend_labels = []
    for idx, (_, label, color) in enumerate(STACK_COMPONENTS):
        vals = matrix[idx, :]
        bars = ax.bar(
            x,
            vals,
            bottom=bottom,
            width=0.66,
            color=color,
            edgecolor="#333333",
            linewidth=0.25,
            label=label,
        )
        handles.append(bars[0])
        legend_labels.append(label)
        bottom += vals

    ax.set_ylim(0, 1.0)
    ax.set_yticks(np.linspace(0, 1.0, 11))
    ax.yaxis.set_major_formatter(PercentFormatter(xmax=1.0))
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.set_ylabel("Normalized fraction", fontsize=11.5)
    title = "Fine-Grained Top-Down Bounds by Benchmark"
    if geomean_spec is not None and np.isfinite(geomean_spec):
        title += f" (SPECint Geomean: {geomean_spec:.2f})"
    ax.set_title(title, fontsize=17, fontweight="bold", pad=10)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=90, ha="right", fontsize=8.5)

    meta_ax = fig.add_subplot(gs[1, 0])
    draw_microarch_config_panel(
        meta_ax,
        config_meta,
        legend_handles=handles,
        legend_labels=legend_labels,
        legend_ncol=len(STACK_COMPONENTS),
        legend_y=1.09,
        box_y=0.035,
        box_h=0.80,
        legend_fontsize=9.4,
        title_fontsize=13.4,
        config_fontsize=9.4,
    )

    fig.subplots_adjust(left=0.060, right=0.970, top=0.945, bottom=0.18)
    fig.savefig(out_png, bbox_inches="tight")
    print(f"Wrote detailed TMA figure: {os.path.abspath(out_png)}")


def main():
    parser = argparse.ArgumentParser(
        description="Plot second/third-level Top-Down bounds from perf_report.txt generated by cal_spec.py"
    )
    parser.add_argument("-i", "--input", default="perf_report.txt", help="input perf report path")
    parser.add_argument("-o", "--output", default="perf_topdown_detail.png", help="output png path")
    args = parser.parse_args()

    benches, geomean_spec, config_meta = parse_perf_report(args.input)
    calc_geomean = compute_geomean_from_spec(benches)
    if geomean_spec is None and calc_geomean is not None:
        geomean_spec = calc_geomean
    if config_meta.get("_CONFIG_WARNINGS"):
        print(f"[WARN] {config_meta['_CONFIG_WARNINGS']}")
    plot_detail(benches, geomean_spec, args.output, config_meta=config_meta)


if __name__ == "__main__":
    main()
