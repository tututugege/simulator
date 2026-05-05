#!/usr/bin/env python3

import argparse
import csv
import math
import os
import re
from pathlib import Path

os.environ.setdefault("MKL_THREADING_LAYER", "GNU")

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
BENCH_RE = re.compile(r"^(\d+)\.([^_]+)_ref$")
PAIR_WITH_PCT_RE = re.compile(r"([A-Za-z0-9_]+)=(-?\d+)\(([-0-9.]+)%\)")


ROOT1_KEYS = [
    "reset",
    "refetch",
    "icache_miss",
    "icache_latency",
    "bpu_stall",
    "fetch_addr_empty",
    "ptab_empty",
    "dummy_ptab",
    "inst_fifo_other",
    "other",
]

ROOT2_KEYS = [
    "reset",
    "recovery_backend_refetch",
    "recovery_frontend_flush",
    "fetch_stall",
    "glue_or_fifo",
    "bpu_side",
    "other",
]

ROOT3_KEYS = [
    "reset",
    "recovery_backend_refetch",
    "recovery_frontend_flush",
    "fetch_stall",
    "glue_or_fifo",
    "bpu_side",
    "other",
]

LATENCY_KEYS = ["tlb_retry", "tlb_fault", "cache_backpressure", "other"]
ITLB_RETRY_KEYS = ["other_walk_active", "walk_req_blocked", "wait_walk_resp", "local_walker_busy"]


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def parse_count_pct_line(line: str) -> dict:
    result = {}
    for key, count, _pct in PAIR_WITH_PCT_RE.findall(line):
        result[key] = int(count)
    return result


def find_line(lines, prefix: str):
    for line in lines:
        if line.startswith(prefix):
            return line
    return None


def parse_int_field(line: str, key: str) -> int:
    m = re.search(rf"(?:^|\s){re.escape(key)}=(-?\d+)(?:\s|$)", line)
    if not m:
        return 0
    return int(m.group(1))


def parse_branch_block(text: str, block: str):
    pattern = re.compile(
        rf"{block}\s+accuracy\s*:\s*([^\n]+)\n\s*num\s*:\s*(\d+)\n\s*mispred\s*:\s*(\d+)",
        re.MULTILINE,
    )
    m = pattern.search(text)
    if not m:
        return None
    accuracy_raw = m.group(1).strip()
    num = int(m.group(2))
    mispred = int(m.group(3))
    accuracy = math.nan
    if "nan" not in accuracy_raw.lower():
        try:
            accuracy = float(accuracy_raw)
        except ValueError:
            accuracy = math.nan
    return {"accuracy": accuracy, "num": num, "mispred": mispred}


def parse_log(path: Path):
    raw = path.read_text(errors="ignore")
    text = strip_ansi(raw)
    lines = [line.strip() for line in text.splitlines()]

    throughput_line = find_line(lines, "throughput:")
    root1_line = find_line(lines, "bubble_root_share(on demand bubbles):")
    root2_line = find_line(lines, "bubble_root2_share(on demand bubbles):")
    root3_line = find_line(lines, "bubble_root3_share(on demand bubbles):")
    latency_line = find_line(lines, "icache_latency_detail(on icache_latency bubbles):")
    itlb_retry_line = find_line(lines, "itlb_retry_detail(on tlb_retry bubbles):")

    if not all([throughput_line, root1_line, root2_line, root3_line, latency_line, itlb_retry_line]):
        return None

    throughput = {
        "demand": parse_int_field(throughput_line, "demand"),
        "deliver": parse_int_field(throughput_line, "deliver"),
        "bubble": parse_int_field(throughput_line, "bubble"),
    }
    root1 = parse_count_pct_line(root1_line)
    root2 = parse_count_pct_line(root2_line)
    root3 = parse_count_pct_line(root3_line)
    latency = parse_count_pct_line(latency_line)
    itlb_retry = parse_count_pct_line(itlb_retry_line)

    branch_bpu = re.search(r"bpu\s+accuracy\s*:\s*([^\n]+)", text)
    bpu_accuracy = math.nan
    if branch_bpu:
        bpu_raw = branch_bpu.group(1).strip()
        if "nan" not in bpu_raw.lower():
            try:
                bpu_accuracy = float(bpu_raw)
            except ValueError:
                bpu_accuracy = math.nan

    jalr = parse_branch_block(text, "jalr")
    br = parse_branch_block(text, "br")
    ret = parse_branch_block(text, "ret")
    if not all([jalr, br, ret]):
        return None

    return {
        "throughput": throughput,
        "root1": root1,
        "root2": root2,
        "root3": root3,
        "latency": latency,
        "itlb_retry": itlb_retry,
        "bpu_accuracy": bpu_accuracy,
        "jalr": jalr,
        "br": br,
        "ret": ret,
    }


def init_bench_stat(name: str, bench_id: int):
    return {
        "benchmark": name,
        "bench_id": bench_id,
        "logs": 0,
        "demand": 0,
        "deliver": 0,
        "bubble": 0,
        "root1": {k: 0 for k in ROOT1_KEYS},
        "root2": {k: 0 for k in ROOT2_KEYS},
        "root3": {k: 0 for k in ROOT3_KEYS},
        "latency": {k: 0 for k in LATENCY_KEYS},
        "itlb_retry": {k: 0 for k in ITLB_RETRY_KEYS},
        "bpu_acc_sum": 0.0,
        "bpu_acc_n": 0,
        "jalr_num": 0,
        "jalr_mispred": 0,
        "br_num": 0,
        "br_mispred": 0,
        "ret_num": 0,
        "ret_mispred": 0,
    }


def safe_ratio(numer: float, denom: float):
    if denom == 0:
        return math.nan
    return numer / denom


def finalize_metrics(stat: dict):
    demand = stat["demand"]
    deliver = stat["deliver"]
    bubble = stat["bubble"]

    total_branch_num = stat["jalr_num"] + stat["br_num"] + stat["ret_num"]
    total_branch_mispred = stat["jalr_mispred"] + stat["br_mispred"] + stat["ret_mispred"]

    overall_acc = 1.0 - safe_ratio(total_branch_mispred, total_branch_num) if total_branch_num else math.nan
    br_acc = 1.0 - safe_ratio(stat["br_mispred"], stat["br_num"]) if stat["br_num"] else math.nan
    ret_acc = 1.0 - safe_ratio(stat["ret_mispred"], stat["ret_num"]) if stat["ret_num"] else math.nan
    jalr_acc = 1.0 - safe_ratio(stat["jalr_mispred"], stat["jalr_num"]) if stat["jalr_num"] else math.nan

    fetch_stall_legacy_total = (
        stat["root1"]["icache_miss"]
        + stat["root1"]["icache_latency"]
        + stat["root1"]["bpu_stall"]
        + stat["root1"]["fetch_addr_empty"]
        + stat["root1"]["ptab_empty"]
        + stat["root1"]["dummy_ptab"]
        + stat["root1"]["inst_fifo_other"]
        + stat["root1"]["other"]
    )

    icache_latency = stat["root1"]["icache_latency"]
    tlb_retry = min(stat["latency"]["tlb_retry"], icache_latency)
    wait_walk_resp = min(stat["itlb_retry"]["wait_walk_resp"], tlb_retry)
    itlb_other_retry = max(tlb_retry - wait_walk_resp, 0)
    latency_non_itlb = max(icache_latency - tlb_retry, 0)
    fetch_other = (
        stat["root1"]["bpu_stall"]
        + stat["root1"]["ptab_empty"]
        + stat["root1"]["dummy_ptab"]
        + stat["root1"]["other"]
    )

    stat["metrics"] = {
        "bubble_pct_demand": safe_ratio(bubble, demand),
        "deliver_pct_demand": safe_ratio(deliver, demand),
        "overall_acc": overall_acc,
        "br_acc": br_acc,
        "ret_acc": ret_acc,
        "jalr_acc": jalr_acc,
        "bpu_acc_mean": safe_ratio(stat["bpu_acc_sum"], stat["bpu_acc_n"]) if stat["bpu_acc_n"] else math.nan,
        "fetch_stall_legacy_total": fetch_stall_legacy_total,
        "fs_icache_miss": stat["root1"]["icache_miss"],
        "fs_icache_latency_itlb_wait": wait_walk_resp,
        "fs_icache_latency_itlb_other": itlb_other_retry,
        "fs_icache_latency_non_itlb": latency_non_itlb,
        "fs_fetch_addr_empty": stat["root1"]["fetch_addr_empty"],
        "fs_inst_fifo_other": stat["root1"]["inst_fifo_other"],
        "fs_other": fetch_other,
    }


def collect_stats(input_dir: Path):
    stats = {}
    for bench_dir in sorted(input_dir.iterdir()):
        if not bench_dir.is_dir():
            continue
        m = BENCH_RE.match(bench_dir.name)
        if not m:
            continue
        bench_id = int(m.group(1))
        bench_name = f"{m.group(1)}.{m.group(2)}"
        stat = init_bench_stat(bench_name, bench_id)

        for log_path in sorted(bench_dir.glob("*.log")):
            parsed = parse_log(log_path)
            if parsed is None:
                continue

            stat["logs"] += 1
            stat["demand"] += int(parsed["throughput"].get("demand", 0))
            stat["deliver"] += int(parsed["throughput"].get("deliver", 0))
            stat["bubble"] += int(parsed["throughput"].get("bubble", 0))

            for key in ROOT1_KEYS:
                stat["root1"][key] += int(parsed["root1"].get(key, 0))
            for key in ROOT2_KEYS:
                stat["root2"][key] += int(parsed["root2"].get(key, 0))
            for key in ROOT3_KEYS:
                stat["root3"][key] += int(parsed["root3"].get(key, 0))
            for key in LATENCY_KEYS:
                stat["latency"][key] += int(parsed["latency"].get(key, 0))
            for key in ITLB_RETRY_KEYS:
                stat["itlb_retry"][key] += int(parsed["itlb_retry"].get(key, 0))

            if not math.isnan(parsed["bpu_accuracy"]):
                stat["bpu_acc_sum"] += parsed["bpu_accuracy"]
                stat["bpu_acc_n"] += 1

            stat["jalr_num"] += parsed["jalr"]["num"]
            stat["jalr_mispred"] += parsed["jalr"]["mispred"]
            stat["br_num"] += parsed["br"]["num"]
            stat["br_mispred"] += parsed["br"]["mispred"]
            stat["ret_num"] += parsed["ret"]["num"]
            stat["ret_mispred"] += parsed["ret"]["mispred"]

        if stat["logs"] > 0:
            finalize_metrics(stat)
            stats[bench_name] = stat

    ordered = sorted(stats.values(), key=lambda x: x["bench_id"])
    return ordered


def to_percent(v):
    if math.isnan(v):
        return math.nan
    return v * 100.0


def draw_plots(stats, output_path: Path, title_prefix: str, dpi: int):
    labels = [s["benchmark"] for s in stats]
    n = len(stats)
    x = np.arange(n)

    fig, axes = plt.subplots(3, 1, figsize=(max(22, 1.8 * n), 22), constrained_layout=True)

    ax1 = axes[0]
    width = 0.24

    deliver_pct = np.array([to_percent(s["metrics"]["deliver_pct_demand"]) for s in stats])
    bubble_pct = np.array([to_percent(s["metrics"]["bubble_pct_demand"]) for s in stats])

    legacy_bottom = np.zeros(n)
    ax1.bar(x - width, deliver_pct, width, label="Legacy: Deliver", color="#4C78A8", bottom=legacy_bottom)
    legacy_bottom += deliver_pct
    ax1.bar(x - width, bubble_pct, width, label="Legacy: Bubble", color="#E45756", bottom=legacy_bottom)

    root2_order = [
        "reset",
        "recovery_backend_refetch",
        "recovery_frontend_flush",
        "fetch_stall",
        "glue_or_fifo",
        "bpu_side",
        "other",
    ]
    root2_names = {
        "reset": "Root2: reset",
        "recovery_backend_refetch": "Root2: recovery_backend_refetch",
        "recovery_frontend_flush": "Root2: recovery_frontend_flush",
        "fetch_stall": "Root2: fetch_stall",
        "glue_or_fifo": "Root2: glue_or_fifo",
        "bpu_side": "Root2: bpu_side",
        "other": "Root2: other",
    }
    root2_colors = {
        "reset": "#BAB0AC",
        "recovery_backend_refetch": "#F58518",
        "recovery_frontend_flush": "#FF9DA6",
        "fetch_stall": "#72B7B2",
        "glue_or_fifo": "#54A24B",
        "bpu_side": "#EECA3B",
        "other": "#B279A2",
    }

    root2_bottom = np.zeros(n)
    ax1.bar(x, deliver_pct, width, label="Root2: Deliver", color="#4C78A8", bottom=root2_bottom, alpha=0.85)
    root2_bottom += deliver_pct
    for key in root2_order:
        vals = np.array(
            [to_percent(safe_ratio(s["root2"].get(key, 0), s["demand"])) if s["demand"] else math.nan for s in stats]
        )
        ax1.bar(x, vals, width, label=root2_names[key], color=root2_colors[key], bottom=root2_bottom)
        root2_bottom += np.nan_to_num(vals, nan=0.0)

    root3_order = [
        "reset",
        "recovery_backend_refetch",
        "recovery_frontend_flush",
        "fetch_stall",
        "glue_or_fifo",
        "bpu_side",
        "other",
    ]
    root3_names = {
        "reset": "Root3: reset",
        "recovery_backend_refetch": "Root3: recovery_backend_refetch",
        "recovery_frontend_flush": "Root3: recovery_frontend_flush",
        "fetch_stall": "Root3: fetch_stall",
        "glue_or_fifo": "Root3: glue_or_fifo",
        "bpu_side": "Root3: bpu_side",
        "other": "Root3: other",
    }

    root3_bottom = np.zeros(n)
    ax1.bar(
        x + width,
        deliver_pct,
        width,
        label="Root3: Deliver",
        color="#4C78A8",
        bottom=root3_bottom,
        alpha=0.85,
    )
    root3_bottom += deliver_pct
    for key in root3_order:
        vals = np.array(
            [to_percent(safe_ratio(s["root3"].get(key, 0), s["demand"])) if s["demand"] else math.nan for s in stats]
        )
        ax1.bar(
            x + width,
            vals,
            width,
            label=root3_names[key],
            color=root2_colors[key],
            bottom=root3_bottom,
            alpha=0.72,
            hatch="//",
        )
        root3_bottom += np.nan_to_num(vals, nan=0.0)

    ax1.set_title(
        f"{title_prefix} Top Panel: Legacy + Root2(demand%) + Root3(demand%)"
    )
    ax1.set_ylabel("Percentage (%)")
    ax1.set_ylim(0, 100)
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, rotation=30, ha="right")
    ax1.grid(axis="y", linestyle="--", alpha=0.3)
    ax1.legend(ncol=6, fontsize=8.5, loc="upper center", bbox_to_anchor=(0.5, 1.36))

    ax2 = axes[1]
    w2 = 0.19
    overall = np.array([to_percent(s["metrics"]["overall_acc"]) for s in stats])
    cond = np.array([to_percent(s["metrics"]["br_acc"]) for s in stats])
    ret = np.array([to_percent(s["metrics"]["ret_acc"]) for s in stats])
    indirect = np.array([to_percent(s["metrics"]["jalr_acc"]) for s in stats])

    ax2.bar(x - 1.5 * w2, overall, w2, label="Overall", color="#4C78A8")
    ax2.bar(x - 0.5 * w2, cond, w2, label="Conditional (br)", color="#F58518")
    ax2.bar(x + 0.5 * w2, ret, w2, label="Return (ret)", color="#54A24B")
    ax2.bar(x + 1.5 * w2, indirect, w2, label="Indirect (jalr)", color="#E45756")

    ax2.set_title(f"{title_prefix} Middle Panel: Branch Prediction Accuracy Breakdown")
    ax2.set_ylabel("Accuracy (%)")
    ax2.set_ylim(0, 100)
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, rotation=30, ha="right")
    ax2.grid(axis="y", linestyle="--", alpha=0.3)
    ax2.legend(ncol=4, loc="upper center", bbox_to_anchor=(0.5, 1.20))

    ax3 = axes[2]
    breakdown_order = [
        "fs_icache_miss",
        "fs_icache_latency_itlb_wait",
        "fs_icache_latency_itlb_other",
        "fs_icache_latency_non_itlb",
        "fs_fetch_addr_empty",
        "fs_inst_fifo_other",
        "fs_other",
    ]
    breakdown_names = {
        "fs_icache_miss": "icache_miss",
        "fs_icache_latency_itlb_wait": "icache_latency: itlb_wait_walk_resp",
        "fs_icache_latency_itlb_other": "icache_latency: itlb_other_retry",
        "fs_icache_latency_non_itlb": "icache_latency: non_itlb",
        "fs_fetch_addr_empty": "fetch_addr_empty",
        "fs_inst_fifo_other": "inst_fifo_other",
        "fs_other": "other(bpu/ptab/etc)",
    }
    breakdown_colors = {
        "fs_icache_miss": "#4C78A8",
        "fs_icache_latency_itlb_wait": "#F58518",
        "fs_icache_latency_itlb_other": "#ECA82C",
        "fs_icache_latency_non_itlb": "#72B7B2",
        "fs_fetch_addr_empty": "#54A24B",
        "fs_inst_fifo_other": "#B279A2",
        "fs_other": "#BAB0AC",
    }

    btm = np.zeros(n)
    for key in breakdown_order:
        vals = []
        for s in stats:
            denom = s["metrics"]["fetch_stall_legacy_total"]
            vals.append(to_percent(safe_ratio(s["metrics"][key], denom)) if denom else math.nan)
        vals = np.array(vals)
        ax3.bar(x, vals, 0.72, bottom=btm, label=breakdown_names[key], color=breakdown_colors[key])
        btm += np.nan_to_num(vals, nan=0.0)

    ax3.set_title(
        f"{title_prefix} Bottom Panel: Fetch-Stall Breakdown "
        "(legacy fetch-stall-like bubbles normalized to 100%)"
    )
    ax3.set_ylabel("Percentage of fetch-stall-like bubbles (%)")
    ax3.set_ylim(0, 100)
    ax3.set_xticks(x)
    ax3.set_xticklabels(labels, rotation=30, ha="right")
    ax3.grid(axis="y", linestyle="--", alpha=0.3)
    ax3.legend(ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.22))

    fig.savefig(output_path, dpi=dpi)
    plt.close(fig)


def write_csv(stats, csv_path: Path):
    header = [
        "benchmark",
        "logs",
        "demand",
        "deliver",
        "bubble",
        "deliver_pct_demand",
        "bubble_pct_demand",
        "l1_reset_pct_demand",
        "l1_recovery_backend_refetch_pct_demand",
        "l1_recovery_frontend_flush_pct_demand",
        "l1_fetch_stall_pct_demand",
        "l1_glue_or_fifo_pct_demand",
        "l1_bpu_side_pct_demand",
        "l1_other_pct_demand",
        "overall_acc",
        "br_acc",
        "ret_acc",
        "jalr_acc",
        "jalr_num",
        "fetch_stall_legacy_total",
        "fs_icache_miss_pct",
        "fs_icache_latency_itlb_wait_pct",
        "fs_icache_latency_itlb_other_pct",
        "fs_icache_latency_non_itlb_pct",
        "fs_fetch_addr_empty_pct",
        "fs_inst_fifo_other_pct",
        "fs_other_pct",
    ]

    with csv_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for s in stats:
            demand = s["demand"]
            fs_total = s["metrics"]["fetch_stall_legacy_total"]
            row = [
                s["benchmark"],
                s["logs"],
                s["demand"],
                s["deliver"],
                s["bubble"],
                s["metrics"]["deliver_pct_demand"],
                s["metrics"]["bubble_pct_demand"],
                safe_ratio(s["root2"]["reset"], demand),
                safe_ratio(s["root2"]["recovery_backend_refetch"], demand),
                safe_ratio(s["root2"]["recovery_frontend_flush"], demand),
                safe_ratio(s["root2"]["fetch_stall"], demand),
                safe_ratio(s["root2"]["glue_or_fifo"], demand),
                safe_ratio(s["root2"]["bpu_side"], demand),
                safe_ratio(s["root2"]["other"], demand),
                s["metrics"]["overall_acc"],
                s["metrics"]["br_acc"],
                s["metrics"]["ret_acc"],
                s["metrics"]["jalr_acc"],
                s["jalr_num"],
                fs_total,
                safe_ratio(s["metrics"]["fs_icache_miss"], fs_total),
                safe_ratio(s["metrics"]["fs_icache_latency_itlb_wait"], fs_total),
                safe_ratio(s["metrics"]["fs_icache_latency_itlb_other"], fs_total),
                safe_ratio(s["metrics"]["fs_icache_latency_non_itlb"], fs_total),
                safe_ratio(s["metrics"]["fs_fetch_addr_empty"], fs_total),
                safe_ratio(s["metrics"]["fs_inst_fifo_other"], fs_total),
                safe_ratio(s["metrics"]["fs_other"], fs_total),
            ]
            writer.writerow(row)


def main():
    parser = argparse.ArgumentParser(description="Plot SPEC2006 front-end bottleneck charts from FALCON logs")
    parser.add_argument("--input", type=Path, default=Path("/home/watts/results_restore"), help="Input directory")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("/home/watts/results_restore/spec2006_frontend_bottleneck.png"),
        help="Output figure path",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=Path("/home/watts/results_restore/spec2006_frontend_bottleneck_summary.csv"),
        help="Output CSV path",
    )
    parser.add_argument("--title-prefix", type=str, default="SPEC2006", help="Title prefix")
    parser.add_argument("--dpi", type=int, default=180, help="Figure DPI")
    args = parser.parse_args()

    stats = collect_stats(args.input)
    if not stats:
        raise SystemExit(f"No valid logs parsed from: {args.input}")

    draw_plots(stats, args.output, args.title_prefix, args.dpi)
    write_csv(stats, args.csv)

    print(f"Parsed benchmarks: {len(stats)}")
    print(f"Figure written to: {args.output}")
    print(f"CSV written to:    {args.csv}")


if __name__ == "__main__":
    main()
