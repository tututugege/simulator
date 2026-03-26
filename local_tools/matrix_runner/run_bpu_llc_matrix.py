#!/usr/bin/env python3
import argparse
import csv
import json
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import List, Optional

SCRIPT_PATH = Path(__file__).resolve()
ROOT = SCRIPT_PATH.parents[2]
DEFAULT_TARGET = Path('baremetal/linux.bin')
DEFAULT_OUTPUT_ROOT = Path('local_logs/matrix_runner')
ERROR_PATTERNS = [
    'Difftest: error',
    'Assertion failed',
    'deadlock',
    'Kernel panic',
    'panic',
    'Oops',
    'abort',
]
FINAL_RE = re.compile(r'Reached committed instruction limit=(\d+) at cycle=(\d+) ipc_total=([0-9.eE+-]+)')
PROGRESS_RE = re.compile(r'\[Progress\] cycle=(\d+) commit=(\d+) ipc_total=([0-9.eE+-]+)(?: ipc_window=([0-9.eE+-]+))?')
REAL_RE = re.compile(r'^real\s+([0-9.]+)$')

@dataclass
class Variant:
    name: str
    bpu: int
    llc: int
    extra_cxxflags: str

@dataclass
class Result:
    variant: str
    bpu: int
    llc: int
    build_dir: str
    build_ok: bool
    build_rc: int
    build_seconds: float
    run_rc: Optional[int]
    run_seconds: Optional[float]
    success: bool
    reached_limit: bool
    run_init_seen: bool
    error_keywords: str
    final_commit: int
    final_cycle: int
    ipc_total: Optional[float]
    ipc_window_last: Optional[float]
    log_path: str
    build_log_path: str
    command: str

VARIANTS: List[Variant] = [
    Variant('bpu0_llc0', 0, 0, ''),
    Variant('bpu0_llc1', 0, 1, '-DCONFIG_AXI_LLC_ENABLE=1'),
    Variant('bpu1_llc0', 1, 0, '-DCONFIG_BPU'),
    Variant('bpu1_llc1', 1, 1, '-DCONFIG_BPU -DCONFIG_AXI_LLC_ENABLE=1'),
]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description='Build/run BPU/LLC matrix and summarize results.')
    p.add_argument('--target', default=str(DEFAULT_TARGET), help='Target binary to run')
    p.add_argument('--max-commit', type=int, default=300_000_000, help='Committed instruction limit')
    p.add_argument('--progress-interval', type=int, default=10_000_000, help='Progress interval in cycles')
    p.add_argument('--build-prefix', default='build_matrix', help='Build dir prefix')
    p.add_argument('--output-root', default=str(DEFAULT_OUTPUT_ROOT), help='Output root for logs/results')
    p.add_argument('--tag', default='', help='Optional suffix for this run directory')
    p.set_defaults(keep_going=True)
    p.add_argument('--stop-on-failure', dest='keep_going', action='store_false',
                   help='Stop immediately after the first failing variant')
    return p.parse_args()


def now_stamp() -> str:
    return time.strftime('%Y%m%dT%H%M%S')


def rel_to_root(path: Path) -> str:
    return str(path.resolve().relative_to(ROOT))


def log_status(msg: str) -> None:
    print(msg, flush=True)


def run_command(cmd: List[str], cwd: Path, log_path: Path) -> tuple[int, float]:
    start = time.time()
    with log_path.open('w', encoding='utf-8', errors='replace') as f:
        f.write('CMD: ' + ' '.join(shlex.quote(x) for x in cmd) + '\n')
        f.write('START: ' + time.strftime('%F %T %Z') + '\n\n')
        f.flush()
        proc = subprocess.run(cmd, cwd=str(cwd), stdout=f, stderr=subprocess.STDOUT)
        end = time.time()
        f.write('\nEND: ' + time.strftime('%F %T %Z') + '\n')
        f.write(f'WALL_SECONDS: {end - start:.2f}\n')
    return proc.returncode, end - start


def start_command(cmd: List[str], cwd: Path, log_path: Path):
    f = log_path.open('w', encoding='utf-8', errors='replace')
    f.write('CMD: ' + ' '.join(shlex.quote(x) for x in cmd) + '\n')
    f.write('START: ' + time.strftime('%F %T %Z') + '\n\n')
    f.flush()
    proc = subprocess.Popen(cmd, cwd=str(cwd), stdout=f, stderr=subprocess.STDOUT)
    return proc, f, time.time()


def parse_run_log(log_path: Path) -> dict:
    text = log_path.read_text(encoding='utf-8', errors='replace')
    final_commit = 0
    final_cycle = 0
    ipc_total = None
    ipc_window_last = None
    reached_limit = False
    run_init_seen = 'Run /init as init process' in text
    error_hits = []
    for pat in ERROR_PATTERNS:
        if pat in text:
            error_hits.append(pat)
    for line in text.splitlines():
        m = FINAL_RE.search(line)
        if m:
            reached_limit = True
            final_commit = int(m.group(1))
            final_cycle = int(m.group(2))
            ipc_total = float(m.group(3))
        m = PROGRESS_RE.search(line)
        if m:
            final_cycle = int(m.group(1))
            final_commit = int(m.group(2))
            ipc_total = float(m.group(3))
            if m.group(4) is not None:
                ipc_window_last = float(m.group(4))
    if ipc_total is None:
        for line in reversed(text.splitlines()):
            m = REAL_RE.match(line.strip())
            if m:
                break
    return {
        'reached_limit': reached_limit,
        'run_init_seen': run_init_seen,
        'error_keywords': ', '.join(error_hits),
        'final_commit': final_commit,
        'final_cycle': final_cycle,
        'ipc_total': ipc_total,
        'ipc_window_last': ipc_window_last,
    }


def write_results(output_dir: Path, results: List[Result]) -> None:
    json_path = output_dir / 'results.json'
    csv_path = output_dir / 'results.csv'
    md_path = output_dir / 'summary.md'
    json_path.write_text(json.dumps([asdict(r) for r in results], indent=2), encoding='utf-8')
    with csv_path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(asdict(results[0]).keys()) if results else [])
        if results:
            writer.writeheader()
            for r in results:
                writer.writerow(asdict(r))
    lines = []
    lines.append('# BPU/LLC Matrix Summary')
    lines.append('')
    lines.append('| variant | build_ok | run_rc | success | commit | cycle | ipc_total | run_s | errors |')
    lines.append('|---|---:|---:|---:|---:|---:|---:|---:|---|')
    for r in results:
        ipc = '' if r.ipc_total is None else f'{r.ipc_total:.6f}'
        run_s = '' if r.run_seconds is None else f'{r.run_seconds:.2f}'
        lines.append(f'| {r.variant} | {int(r.build_ok)} | {"" if r.run_rc is None else r.run_rc} | {int(r.success)} | {r.final_commit} | {r.final_cycle} | {ipc} | {run_s} | {r.error_keywords} |')
    md_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main() -> int:
    args = parse_args()
    target = Path(args.target)
    if not target.is_absolute():
        target = (ROOT / target).resolve()
    target_rel = rel_to_root(target)
    if not target.exists():
        print(f'target not found: {target}', file=sys.stderr)
        return 2

    stamp = now_stamp()
    tag = f'_{args.tag}' if args.tag else ''
    output_root = Path(args.output_root)
    if not output_root.is_absolute():
        output_root = ROOT / output_root
    output_dir = output_root / f'{stamp}_matrix{tag}'
    output_dir.mkdir(parents=True, exist_ok=True)

    meta = {
        'root': '.',
        'target': target_rel,
        'max_commit': args.max_commit,
        'progress_interval': args.progress_interval,
        'started_at': time.strftime('%F %T %Z'),
    }
    (output_dir / 'meta.json').write_text(json.dumps(meta, indent=2), encoding='utf-8')

    results: List[Result] = []
    run_queue = []
    for v in VARIANTS:
        build_dir = f'{args.build_prefix}_{v.name}'
        variant_dir = output_dir / v.name
        variant_dir.mkdir(parents=True, exist_ok=True)
        build_log = variant_dir / 'build.log'
        run_log = variant_dir / 'run.log'
        build_cmd = ['make', '-j', f'BUILD_DIR={build_dir}']
        if v.extra_cxxflags:
            build_cmd.append(f'EXTRA_CXXFLAGS={v.extra_cxxflags}')
        log_status(f'[build start] {v.name} -> {build_dir}')
        build_rc, build_s = run_command(build_cmd, ROOT, build_log)
        log_status(f'[build done] {v.name} rc={build_rc} wall_s={build_s:.2f}')
        if build_rc != 0:
            result = Result(
                variant=v.name, bpu=v.bpu, llc=v.llc, build_dir=build_dir,
                build_ok=False, build_rc=build_rc, build_seconds=build_s,
                run_rc=None, run_seconds=None, success=False,
                reached_limit=False, run_init_seen=False, error_keywords='build_failed',
                final_commit=0, final_cycle=0, ipc_total=None, ipc_window_last=None,
                log_path=rel_to_root(run_log), build_log_path=rel_to_root(build_log), command=''
            )
            results.append(result)
            write_results(output_dir, results)
            if not args.keep_going:
                return 1
            continue

        sim_path = Path(f'./{build_dir}/simulator')
        run_cmd = [
            str(sim_path),
            '--max-commit', str(args.max_commit),
            '--progress-interval', str(args.progress_interval),
            target_rel,
        ]
        result = Result(
            variant=v.name, bpu=v.bpu, llc=v.llc, build_dir=build_dir,
            build_ok=True, build_rc=build_rc, build_seconds=build_s,
            run_rc=None, run_seconds=None, success=False,
            reached_limit=False, run_init_seen=False, error_keywords='',
            final_commit=0, final_cycle=0, ipc_total=None,
            ipc_window_last=None, log_path=rel_to_root(run_log),
            build_log_path=rel_to_root(build_log), command=' '.join(shlex.quote(x) for x in run_cmd)
        )
        results.append(result)
        write_results(output_dir, results)
        run_queue.append((len(results) - 1, run_cmd, run_log))

    running = {}
    log_status(f'[run phase] launching {len(run_queue)} variant(s) in parallel')
    for idx, run_cmd, run_log in run_queue:
        proc, handle, start = start_command(run_cmd, ROOT, run_log)
        running[proc.pid] = {
            'idx': idx,
            'proc': proc,
            'handle': handle,
            'start': start,
            'log_path': run_log,
        }
        log_status(f'[run start] {results[idx].variant} pid={proc.pid}')

    terminate_others = False
    last_running_report = 0.0
    while running:
        done_pids = []
        for pid, info in running.items():
            rc = info['proc'].poll()
            if rc is None:
                continue
            end = time.time()
            info['handle'].write('\nEND: ' + time.strftime('%F %T %Z') + '\n')
            info['handle'].write(f'WALL_SECONDS: {end - info["start"]:.2f}\n')
            info['handle'].close()
            parsed = parse_run_log(info['log_path'])
            result = results[info['idx']]
            result.run_rc = rc
            result.run_seconds = end - info['start']
            result.reached_limit = parsed['reached_limit']
            result.run_init_seen = parsed['run_init_seen']
            result.error_keywords = parsed['error_keywords']
            result.final_commit = parsed['final_commit']
            result.final_cycle = parsed['final_cycle']
            result.ipc_total = parsed['ipc_total']
            result.ipc_window_last = parsed['ipc_window_last']
            result.success = (
                result.build_ok and
                rc == 0 and
                parsed['reached_limit'] and
                parsed['final_commit'] >= args.max_commit and
                not parsed['error_keywords']
            )
            log_status(
                f'[run done] {result.variant} rc={rc} success={int(result.success)} '
                f'commit={result.final_commit} cycle={result.final_cycle} '
                f'ipc_total={"" if result.ipc_total is None else f"{result.ipc_total:.6f}"} '
                f'wall_s={result.run_seconds:.2f}'
            )
            write_results(output_dir, results)
            if (not result.success) and (not args.keep_going):
                terminate_others = True
            done_pids.append(pid)
        for pid in done_pids:
            running.pop(pid, None)

        if terminate_others and running:
            for pid, info in list(running.items()):
                info['proc'].terminate()
                try:
                    info['proc'].wait(timeout=5)
                except subprocess.TimeoutExpired:
                    info['proc'].kill()
                    info['proc'].wait()
                end = time.time()
                info['handle'].write('\nTERMINATED: due to earlier failure\n')
                info['handle'].write('END: ' + time.strftime('%F %T %Z') + '\n')
                info['handle'].write(f'WALL_SECONDS: {end - info["start"]:.2f}\n')
                info['handle'].close()
                result = results[info['idx']]
                result.run_rc = info['proc'].returncode
                result.run_seconds = end - info['start']
                result.error_keywords = 'terminated_due_to_earlier_failure'
                result.success = False
                log_status(f'[run terminated] {result.variant} rc={result.run_rc}')
                write_results(output_dir, results)
                running.pop(pid, None)
            break

        if running and (time.time() - last_running_report >= 5.0):
            active = ', '.join(results[info['idx']].variant for info in running.values())
            log_status(f'[run active] {active}')
            last_running_report = time.time()

        if running:
            time.sleep(1.0)

    log_status(f'[matrix done] summary={rel_to_root(output_dir / "summary.md")}')
    return 0 if all(r.success for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
