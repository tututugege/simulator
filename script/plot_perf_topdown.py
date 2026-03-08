#!/usr/bin/env python3
import argparse
import math
import os
import re
from typing import Dict, List, Optional

import matplotlib.pyplot as plt
import numpy as np


def parse_pct(line: str) -> Optional[float]:
    m = re.search(r"([-+]?\d+(?:\.\d+)?)\s*%", line)
    return float(m.group(1)) if m else None


def parse_num(line: str) -> Optional[float]:
    m = re.search(r"([-+]?\d+(?:\.\d+)?)", line)
    return float(m.group(1)) if m else None

def parse_num_after_colon(line: str) -> Optional[float]:
    m = re.search(r":\s*([-+]?\d+(?:\.\d+)?)\s*$", line)
    return float(m.group(1)) if m else None


def parse_perf_report(path: str):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = [x.strip() for x in f.readlines()]

    benches: List[Dict[str, float]] = []
    geomean_spec = None

    cur = None
    in_tma = None
    for line in lines:
        if not line:
            continue

        if line.startswith("Benchmark:"):
            # Example: Benchmark: 401.bzip2_ref (key=401.bzip2)
            if cur:
                benches.append(cur)
            bench_label = line.split("Benchmark:", 1)[1].strip()
            bench_label = bench_label.split(" (key=", 1)[0].strip()
            cur = {
                "bench": bench_label,
                "ipc": np.nan,
                "retiring": np.nan,
                "bad_spec": np.nan,
                "frontend": np.nan,
                "backend": np.nan,
                "retiring_core": np.nan,
                "bad_spec_core": np.nan,
                "frontend_core": np.nan,
                "backend_core": np.nan,
                "retiring_idu": np.nan,
                "bad_spec_idu": np.nan,
                "frontend_idu": np.nan,
                "backend_idu": np.nan,
                "spec": np.nan,
            }
            in_tma = None
            continue

        if line.startswith("Final Estimated SPECint2006:"):
            geomean_spec = parse_num_after_colon(line)
            continue

        if cur is None:
            continue

        if line.startswith("Weighted IPC:"):
            v = parse_num(line)
            if v is not None:
                cur["ipc"] = v
            continue

        if line.startswith("SPEC Ratio:"):
            v = parse_num(line)
            if v is not None:
                cur["spec"] = v
            continue

        if line.startswith("TMA (Weighted):"):
            in_tma = "core"
            continue

        if line.startswith("TMA-IDU (Weighted):"):
            in_tma = "idu"
            continue

        if in_tma:
            if line.startswith("Frontend Bound:"):
                v = parse_pct(line)
                if v is not None:
                    if in_tma == "idu":
                        cur["frontend_idu"] = v / 100.0
                    else:
                        cur["frontend_core"] = v / 100.0
            elif line.startswith("Backend Bound:"):
                v = parse_pct(line)
                if v is not None:
                    if in_tma == "idu":
                        cur["backend_idu"] = v / 100.0
                    else:
                        cur["backend_core"] = v / 100.0
            elif line.startswith("Bad Speculation:"):
                v = parse_pct(line)
                if v is not None:
                    if in_tma == "idu":
                        cur["bad_spec_idu"] = v / 100.0
                    else:
                        cur["bad_spec_core"] = v / 100.0
            elif line.startswith("Retiring:"):
                v = parse_pct(line)
                if v is not None:
                    if in_tma == "idu":
                        cur["retiring_idu"] = v / 100.0
                    else:
                        cur["retiring_core"] = v / 100.0

    if cur:
        benches.append(cur)

    # Prefer IDU TMA for plotting. Fallback to core TMA when IDU data is absent.
    for b in benches:
        use_idu = any(
            np.isfinite(v)
            for v in [b["frontend_idu"], b["backend_idu"], b["bad_spec_idu"], b["retiring_idu"]]
        )
        if use_idu:
            b["frontend"] = b["frontend_idu"]
            b["backend"] = b["backend_idu"]
            b["bad_spec"] = b["bad_spec_idu"]
            b["retiring"] = b["retiring_idu"]
        else:
            b["frontend"] = b["frontend_core"]
            b["backend"] = b["backend_core"]
            b["bad_spec"] = b["bad_spec_core"]
            b["retiring"] = b["retiring_core"]

    # remove entries with no core plot info
    valid = [
        b
        for b in benches
        if not (
            np.isnan(b["retiring"])
            and np.isnan(b["bad_spec"])
            and np.isnan(b["frontend"])
            and np.isnan(b["backend"])
        )
    ]
    return valid, geomean_spec


def compute_geomean_from_spec(benches: List[Dict[str, float]]) -> Optional[float]:
    vals = []
    for b in benches:
        v = b.get("spec", np.nan)
        if np.isfinite(v) and v > 0:
            vals.append(float(v))
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def plot_report(benches: List[Dict[str, float]], geomean_spec: Optional[float], out_png: str):
    if not benches:
        raise RuntimeError("No benchmark data parsed from perf report.")

    labels = [b["bench"] for b in benches]
    retiring = np.array([b["retiring"] for b in benches], dtype=float)
    bad_spec = np.array([b["bad_spec"] for b in benches], dtype=float)
    frontend = np.array([b["frontend"] for b in benches], dtype=float)
    backend = np.array([b["backend"] for b in benches], dtype=float)
    ipc = np.array([b["ipc"] for b in benches], dtype=float)
    spec = np.array([b["spec"] for b in benches], dtype=float)

    # Keep top/bottom x-axis aligned even when we append GEOMEAN in SPEC subplot.
    axis_labels = labels.copy()
    if geomean_spec is not None and np.isfinite(geomean_spec):
        axis_labels.append("GEOMEAN")

    x = np.arange(len(axis_labels))
    n = len(labels)

    retiring_plot = np.zeros(len(axis_labels), dtype=float)
    bad_spec_plot = np.zeros(len(axis_labels), dtype=float)
    frontend_plot = np.zeros(len(axis_labels), dtype=float)
    backend_plot = np.zeros(len(axis_labels), dtype=float)
    ipc_plot = np.full(len(axis_labels), np.nan, dtype=float)
    retiring_plot[:n] = retiring
    bad_spec_plot[:n] = bad_spec
    frontend_plot[:n] = frontend
    backend_plot[:n] = backend
    ipc_plot[:n] = ipc

    fig = plt.figure(figsize=(max(12, len(labels) * 0.55), 9), dpi=140)
    gs = fig.add_gridspec(2, 1, height_ratios=[3.2, 1.6], hspace=0.40)

    ax = fig.add_subplot(gs[0, 0])
    c_ret = "#92b558"
    c_bad = "#c0504d"
    c_fe = "#7030a0"
    c_be = "#f4b400"
    c_ipc = "#2aa7d6"

    b1 = ax.bar(x, retiring_plot, color=c_ret, width=0.66, label="Retiring")
    b2 = ax.bar(x, bad_spec_plot, bottom=retiring_plot, color=c_bad, width=0.66, label="Bad Speculation")
    b3 = ax.bar(x, frontend_plot, bottom=retiring_plot + bad_spec_plot, color=c_fe, width=0.66, label="Frontend Bound")
    b4 = ax.bar(
        x,
        backend_plot,
        bottom=retiring_plot + bad_spec_plot + frontend_plot,
        color=c_be,
        width=0.66,
        label="Backend Bound",
    )
    _ = (b1, b2, b3, b4)

    ax.set_ylim(0, 1.0)
    ax.set_yticks(np.linspace(0, 1.0, 11))
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.set_ylabel("Top-Down Fraction")
    ax.set_title("Top Level Breakdown (IDU TMA) + IPC")

    ax2 = ax.twinx()
    line = ax2.plot(
        x,
        ipc_plot,
        color=c_ipc,
        marker="^",
        linestyle=":",
        linewidth=1.8,
        markersize=5.0,
        label="IPC",
    )[0]
    ax2.set_ylim(0, 8.0)
    ax2.set_yticks(np.arange(0, 9, 1))
    ax2.set_ylabel("IPC")

    # Label IPC value on each line marker.
    for xi, yi in zip(x, ipc_plot):
        if np.isfinite(yi):
            ax2.text(xi, yi + 0.03, f"{yi:.2f}", ha="center", va="bottom",
                     fontsize=8, color=c_ipc)

    ax.set_xticks(x)
    # Keep aligned ticks but hide top labels for cleaner view.
    ax.set_xticklabels([])

    handles1, labels1 = ax.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(handles1 + handles2, labels1 + labels2, ncol=5, loc="upper center", fontsize=9)

    axs = fig.add_subplot(gs[1, 0])
    spec_labels = axis_labels.copy()
    spec_vals = spec.copy()
    if len(spec_labels) > len(spec_vals):
        spec_vals = np.append(spec_vals, geomean_spec)
    x_spec = np.arange(len(spec_labels))

    bar_colors = ["#4e79a7"] * len(spec_vals)
    if len(spec_vals) > len(spec):
        bar_colors[-1] = "#d62728"

    bars = axs.bar(x_spec, spec_vals, color=bar_colors, width=0.66, label="SPEC Ratio")
    axs.set_ylabel("SPEC Ratio")
    axs.set_title("Per-Benchmark SPEC Ratio")
    axs.grid(axis="y", linestyle="--", alpha=0.35)
    axs.set_xticks(x_spec)
    axs.set_xticklabels(spec_labels, rotation=90, ha="right", fontsize=8.5)

    if np.isfinite(spec_vals).any():
        axs.set_ylim(0, np.nanmax(spec_vals) * 1.25)

    for r, v in zip(bars, spec_vals):
        if np.isfinite(v):
            axs.text(r.get_x() + r.get_width() / 2, r.get_height(), f"{v:.2f}",
                     ha="center", va="bottom", fontsize=8, rotation=0)

    if geomean_spec is not None:
        axs.axhline(geomean_spec, color="#d62728", linestyle="--", linewidth=1.4)
        axs.text(
            0.01,
            0.96,
            f"SPECint Geomean: {geomean_spec:.2f}",
            transform=axs.transAxes,
            ha="left",
            va="top",
            fontsize=9,
            color="#d62728",
        )

    fig.tight_layout()
    fig.savefig(out_png, bbox_inches="tight")
    print(f"Wrote figure: {os.path.abspath(out_png)}")


def main():
    parser = argparse.ArgumentParser(
        description="Plot Top-Down/IPC/SPEC from perf_report.txt generated by cal_spec.py"
    )
    parser.add_argument("-i", "--input", default="perf_report.txt", help="input perf report path")
    parser.add_argument("-o", "--output", default="perf_topdown_spec.png", help="output png path")
    args = parser.parse_args()

    benches, geomean_spec = parse_perf_report(args.input)
    calc_geomean = compute_geomean_from_spec(benches)
    if calc_geomean is not None:
        geomean_spec = calc_geomean
        print(f"[INFO] GEOMEAN computed from per-benchmark SPEC ratios: {geomean_spec:.2f}")
    else:
        print("[WARN] Cannot compute GEOMEAN: no valid positive SPEC Ratio values found.")
    plot_report(benches, geomean_spec, args.output)


if __name__ == "__main__":
    main()
