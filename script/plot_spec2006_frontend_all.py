#!/usr/bin/env python3

import argparse
import os
import re
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

from plot_spec2006_frontend import (
    ITLB_RETRY_KEYS,
    LATENCY_KEYS,
    ROOT1_KEYS,
    ROOT2_KEYS,
    ROOT3_KEYS,
    collect_stats,
    draw_plots,
    write_csv,
)


ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
PAIR_WITH_PCT_RE = re.compile(r"([A-Za-z0-9_]+)=(-?\d+)\(([-0-9.]+)%\)")

LINE_PREFIXES = {
    "root1": "bubble_root_share(on demand bubbles):",
    "root2": "bubble_root2_share(on demand bubbles):",
    "root3": "bubble_root3_share(on demand bubbles):",
    "latency": "icache_latency_detail(on icache_latency bubbles):",
    "itlb_retry": "itlb_retry_detail(on tlb_retry bubbles):",
}

EXPECTED_KEYS = {
    "root1": set(ROOT1_KEYS),
    "root2": set(ROOT2_KEYS),
    "root3": set(ROOT3_KEYS),
    "latency": set(LATENCY_KEYS),
    "itlb_retry": set(ITLB_RETRY_KEYS),
}


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def parse_keys_from_line(line: str) -> Set[str]:
    return {key for key, _count, _pct in PAIR_WITH_PCT_RE.findall(line)}


def iter_log_files(config_dir: Path) -> Iterable[Path]:
    for bench_dir in sorted(config_dir.iterdir()):
        if not bench_dir.is_dir():
            continue
        for log_path in sorted(bench_dir.glob("*.log")):
            yield log_path


def detect_falcon_fields(config_dir: Path) -> Tuple[bool, Dict[str, Set[str]], Dict[str, int], int]:
    seen_keys: Dict[str, Set[str]] = {k: set() for k in LINE_PREFIXES}
    missing_counts: Dict[str, int] = {k: 0 for k in LINE_PREFIXES}
    matched_logs = 0

    for log_path in iter_log_files(config_dir):
        text = strip_ansi(log_path.read_text(errors="ignore"))
        lines = [line.strip() for line in text.splitlines()]

        line_map = {}
        for name, prefix in LINE_PREFIXES.items():
            line = next((l for l in lines if l.startswith(prefix)), None)
            line_map[name] = line

        if all(line_map.values()):
            matched_logs += 1
            for name, line in line_map.items():
                seen_keys[name] |= parse_keys_from_line(line)
        else:
            for name, line in line_map.items():
                if line is None:
                    missing_counts[name] += 1

    all_expected_found = matched_logs > 0 and all(
        EXPECTED_KEYS[name].issubset(seen_keys[name]) for name in LINE_PREFIXES
    )
    return all_expected_found, seen_keys, missing_counts, matched_logs


def find_config_dirs(root: Path) -> List[Path]:
    config_dirs = []
    for path in sorted(root.iterdir()):
        if not path.is_dir():
            continue
        has_logs = any(path.glob("*/*.log"))
        if has_logs:
            config_dirs.append(path)
    return config_dirs


def output_paths(
    config_dir: Path,
    output_name: str,
    csv_name: str,
    output_dir: Optional[Path],
) -> Tuple[Path, Path]:
    if output_dir is None:
        return config_dir / output_name, config_dir / csv_name

    output_dir.mkdir(parents=True, exist_ok=True)
    config_name = config_dir.name
    return (
        output_dir / f"{config_name}_{output_name}",
        output_dir / f"{config_name}_{csv_name}",
    )


def main():
    parser = argparse.ArgumentParser(
        description="Batch plot SPEC2006 falcon frontend charts for all config folders under a root directory."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Root directory containing config subfolders (default: script directory)",
    )
    parser.add_argument(
        "--output-name",
        default="spec2006_frontend_bottleneck.png",
        help="Output figure filename when writing inside each config folder",
    )
    parser.add_argument(
        "--csv-name",
        default="spec2006_frontend_bottleneck_summary.csv",
        help="Output CSV filename when writing inside each config folder",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Optional shared output directory (config name will be prefixed)",
    )
    parser.add_argument("--dpi", type=int, default=180, help="Figure DPI")
    args = parser.parse_args()

    root = args.root.resolve()
    config_dirs = find_config_dirs(root)
    if not config_dirs:
        raise RuntimeError(f"No config directories with logs found under: {root}")

    print(f"[INFO] root: {root}")
    print(f"[INFO] found {len(config_dirs)} config directories")

    plotted = 0
    skipped = 0

    for config_dir in config_dirs:
        ok, seen_keys, missing_counts, matched_logs = detect_falcon_fields(config_dir)
        if not ok:
            print(
                f"[WARN] skip {config_dir.name}: falcon fields incomplete or absent "
                f"(matched_logs={matched_logs}, missing={missing_counts})"
            )
            skipped += 1
            continue

        stats = collect_stats(config_dir)
        if not stats:
            print(f"[WARN] skip {config_dir.name}: no valid falcon stats parsed")
            skipped += 1
            continue

        output_png, output_csv = output_paths(
            config_dir=config_dir,
            output_name=args.output_name,
            csv_name=args.csv_name,
            output_dir=args.output_dir,
        )

        draw_plots(stats, output_png, config_dir.name, args.dpi)
        write_csv(stats, output_csv)
        print(
            f"[INFO] plotted {config_dir.name}: benches={len(stats)} "
            f"png={output_png} csv={output_csv}"
        )
        plotted += 1

        for group_name, expected in EXPECTED_KEYS.items():
            extras = sorted(seen_keys[group_name] - expected)
            if extras:
                print(f"[INFO] {config_dir.name} extra keys in {group_name}: {extras}")

    if plotted == 0:
        raise RuntimeError("No falcon figure generated. Please check input logs.")
    print(f"[INFO] done. plotted={plotted}, skipped={skipped}")


if __name__ == "__main__":
    os.environ.setdefault("MKL_THREADING_LAYER", "GNU")
    os.environ.setdefault("MPLBACKEND", "Agg")
    main()
