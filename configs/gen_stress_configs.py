#!/usr/bin/env python3
import argparse
import dataclasses
import pathlib
import random
import re
import subprocess
import sys
from typing import Dict, List, Tuple


ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG_H = ROOT / "include" / "config.h"
OUT_DIR = ROOT / "configs" / "stress_generated"
SIM_BIN = ROOT / "build" / "simulator"
DHRY_BIN = ROOT / "baremetal" / "dhrystone" / "build" / "dhrystone.bin"


@dataclasses.dataclass(frozen=True)
class StressConfig:
    name: str
    cache_profile: str
    backend_profile: str
    ftq_profile: str
    fu_profile: str
    issue_profile: str
    ic_miss: int
    dc_miss: int
    dc_index: int
    dc_way: int
    rob_num: int
    prf_num: int
    ldq_size: int
    stq_size: int
    ftq_size: int
    int_alu_ports: int
    int_muldiv_ports: int
    alu_simple_ports: int
    ld_ports: int
    sta_ports: int
    std_ports: int
    br_ports: int


LINE_PATTERNS: Dict[str, re.Pattern] = {
    "ic_miss": re.compile(r"^constexpr int ICACHE_MISS_LATENCY = .*?;\s*(?://.*)?$", re.M),
    "dc_miss": re.compile(r"^constexpr int DCACHE_MEM_LATENCY = .*?;\s*(?://.*)?$", re.M),
    "dc_index": re.compile(r"^constexpr int DCACHE_INDEX_BITS = .*?;\s*(?://.*)?$", re.M),
    "dc_way": re.compile(r"^constexpr int DCACHE_WAY_NUM = .*?;\s*(?://.*)?$", re.M),
    "rob_num": re.compile(r"^constexpr int ROB_NUM = .*?;\s*(?://.*)?$", re.M),
    "prf_num": re.compile(r"^constexpr int PRF_NUM = .*?;\s*(?://.*)?$", re.M),
    "ldq_size": re.compile(r"^constexpr int LDQ_SIZE = .*?;\s*(?://.*)?$", re.M),
    "stq_size": re.compile(r"^constexpr int STQ_SIZE = .*?;\s*(?://.*)?$", re.M),
    "ftq_size": re.compile(r"^constexpr int FTQ_SIZE = .*?;\s*(?://.*)?$", re.M),
}

ISSUE_BLOCK_PATTERN = re.compile(
    r"constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG\[\] = \{.*?\n\};",
    re.S,
)


SECTION_POOL = {
    "cache": {
        "balanced": {"ic_miss": 60, "dc_miss": 60, "dc_index": 8, "dc_way": 4},
        "fast": {"ic_miss": 30, "dc_miss": 30, "dc_index": 9, "dc_way": 8},
        "slow": {"ic_miss": 90, "dc_miss": 90, "dc_index": 7, "dc_way": 2},
        "ic_pressure": {"ic_miss": 100, "dc_miss": 50, "dc_index": 8, "dc_way": 4},
        "dc_pressure": {"ic_miss": 50, "dc_miss": 100, "dc_index": 7, "dc_way": 2},
    },
    "backend": {
        "base": {"rob_num": 256, "prf_num": 160, "ldq_size": 64, "stq_size": 64},
        "compact": {"rob_num": 128, "prf_num": 128, "ldq_size": 32, "stq_size": 32},
        "memory_heavy": {"rob_num": 256, "prf_num": 192, "ldq_size": 96, "stq_size": 64},
        "large_window": {"rob_num": 384, "prf_num": 224, "ldq_size": 64, "stq_size": 64},
    },
    "ftq": {
        "base": {"ftq_size": 64},
        "small": {"ftq_size": 32},
        "large": {"ftq_size": 128},
    },
    "fu": {
        # Hard constraints we keep:
        # 1) CSR fixed on Port0.
        # 2) INT IQ visible ports are all ALU-capable.
        # 3) MUL/DIV share ALU ports.
        "compact": {"int_alu_ports": 3, "int_muldiv_ports": 1},
        "balanced": {"int_alu_ports": 4, "int_muldiv_ports": 2},
        "wide": {"int_alu_ports": 6, "int_muldiv_ports": 3},
    },
    "issue": {
        # Non-ALU issue-domain counts. Each class must remain >=1.
        "base": {"ld_ports": 2, "sta_ports": 2, "std_ports": 2, "br_ports": 2},
        "mem_dense": {"ld_ports": 3, "sta_ports": 2, "std_ports": 2, "br_ports": 2},
        "branch_dense": {"ld_ports": 2, "sta_ports": 2, "std_ports": 2, "br_ports": 3},
        "compact": {"ld_ports": 1, "sta_ports": 1, "std_ports": 1, "br_ports": 1},
    },
}


def replace_one(text: str, key: str, value: int) -> str:
    repl = {
        "ic_miss": f"constexpr int ICACHE_MISS_LATENCY = {value};",
        "dc_miss": f"constexpr int DCACHE_MEM_LATENCY = {value};",
        "dc_index": f"constexpr int DCACHE_INDEX_BITS = {value};",
        "dc_way": f"constexpr int DCACHE_WAY_NUM = {value};",
        "rob_num": f"constexpr int ROB_NUM = {value};",
        "prf_num": f"constexpr int PRF_NUM = {value};",
        "ldq_size": f"constexpr int LDQ_SIZE = {value};",
        "stq_size": f"constexpr int STQ_SIZE = {value};",
        "ftq_size": f"constexpr int FTQ_SIZE = {value};",
    }[key]
    out, n = LINE_PATTERNS[key].subn(repl, text, count=1)
    if n != 1:
        raise RuntimeError(f"failed to patch key={key}")
    return out


def build_issue_block(cfg: StressConfig) -> str:
    lines = []
    lines.append("constexpr IssuePortConfigInfo GLOBAL_ISSUE_PORT_CONFIG[] = {")
    # Port0 fixed: keep CSR on port0, and keep mul/div sharing ALU port.
    lines.append("    PORT_CFG(OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV | OP_MASK_CSR), // Port 0")

    # Remaining INT IQ ports are all ALU-capable.
    # First N ports also carry MUL/DIV to model fu density; MUL/DIV always share ALU ports.
    for i in range(max(0, cfg.int_alu_ports - 1)):
        if i < max(0, cfg.int_muldiv_ports - 1):
            lines.append("    PORT_CFG(OP_MASK_ALU | OP_MASK_MUL | OP_MASK_DIV),")
        else:
            lines.append("    PORT_CFG(OP_MASK_ALU),")

    for _ in range(cfg.ld_ports):
        lines.append("    PORT_CFG(OP_MASK_LD),")
    for _ in range(cfg.sta_ports):
        lines.append("    PORT_CFG(OP_MASK_STA),")
    for _ in range(cfg.std_ports):
        lines.append("    PORT_CFG(OP_MASK_STD),")
    for _ in range(cfg.br_ports):
        lines.append("    PORT_CFG(OP_MASK_BR),")

    # Remove trailing comma from final entry.
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines)


def patch_config(base_text: str, cfg: StressConfig) -> str:
    text = base_text
    for key in ("ic_miss", "dc_miss", "dc_index", "dc_way", "rob_num", "prf_num", "ldq_size", "stq_size", "ftq_size"):
        text = replace_one(text, key, getattr(cfg, key))

    issue_block = build_issue_block(cfg)
    text, n = ISSUE_BLOCK_PATTERN.subn(issue_block, text, count=1)
    if n != 1:
        raise RuntimeError("failed to patch GLOBAL_ISSUE_PORT_CONFIG block")
    return text


def is_power_of_two(v: int) -> bool:
    return v > 0 and (v & (v - 1)) == 0


def validate_cfg(cfg: StressConfig) -> Tuple[bool, str]:
    if cfg.rob_num % 8 != 0:
        return False, "ROB_NUM must be multiple of ROB_BANK_NUM(8)"
    if cfg.prf_num < 32:
        return False, "PRF_NUM must be >= ARF_NUM(32)"
    if cfg.stq_size > 64:
        return False, "STQ_SIZE > 64 violates static_assert"
    if cfg.ldq_size <= 0 or cfg.stq_size <= 0:
        return False, "LDQ/STQ non-positive"
    if not is_power_of_two(cfg.ftq_size):
        return False, "FTQ_SIZE must be power of two"
    if cfg.dc_index <= 0 or cfg.dc_way <= 0:
        return False, "DCache index/way invalid"
    if cfg.int_alu_ports < 1:
        return False, "need >=1 ALU-capable INT port"
    if cfg.int_muldiv_ports < 1:
        return False, "need >=1 MUL/DIV port"
    if cfg.int_muldiv_ports > cfg.int_alu_ports:
        return False, "MUL/DIV ports must be subset of ALU ports"
    # Make sure all IQ classes still have at least one supported port.
    if min(cfg.ld_ports, cfg.sta_ports, cfg.std_ports, cfg.br_ports) < 1:
        return False, "issue profile missing mandatory port class"
    return True, ""


def run_cmd(cmd: List[str], timeout_s: int) -> Tuple[int, str]:
    p = subprocess.run(
        cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout_s,
        errors="ignore",
    )
    return p.returncode, p.stdout


def verify(timeout_s: int) -> Tuple[bool, str]:
    rc_make, out_make = run_cmd(["make", "-j"], timeout_s=max(60, timeout_s))
    if rc_make != 0:
        return False, "build fail\n" + "\n".join(out_make.splitlines()[-30:])

    rc_run, out_run = run_cmd([str(SIM_BIN), str(DHRY_BIN)], timeout_s=timeout_s)
    ok = ("Success!!!!" in out_run) and ("Simulation Exited with Reason" in out_run)
    if ok:
        return True, "pass"
    if rc_run != 0:
        return False, f"run rc={rc_run}\n" + "\n".join(out_run.splitlines()[-30:])
    return False, "no Success!!!!\n" + "\n".join(out_run.splitlines()[-30:])


def sample_configs(num: int, seed: int) -> List[StressConfig]:
    rng = random.Random(seed)
    cache_keys = list(SECTION_POOL["cache"].keys())
    back_keys = list(SECTION_POOL["backend"].keys())
    ftq_keys = list(SECTION_POOL["ftq"].keys())
    fu_keys = list(SECTION_POOL["fu"].keys())
    issue_keys = list(SECTION_POOL["issue"].keys())

    picked = []
    seen = set()
    max_try = max(200, num * 50)
    idx = 1
    tries = 0
    while len(picked) < num and tries < max_try:
        tries += 1
        ck = rng.choice(cache_keys)
        bk = rng.choice(back_keys)
        fk = rng.choice(ftq_keys)
        uk = rng.choice(fu_keys)
        ik = rng.choice(issue_keys)
        tup = (ck, bk, fk, uk, ik)
        if tup in seen:
            continue
        seen.add(tup)

        m = {}
        m.update(SECTION_POOL["cache"][ck])
        m.update(SECTION_POOL["backend"][bk])
        m.update(SECTION_POOL["ftq"][fk])
        m.update(SECTION_POOL["fu"][uk])
        m.update(SECTION_POOL["issue"][ik])

        cfg = StressConfig(
            name=f"stress_{idx:02d}",
            cache_profile=ck,
            backend_profile=bk,
            ftq_profile=fk,
            fu_profile=uk,
            issue_profile=ik,
            ic_miss=m["ic_miss"],
            dc_miss=m["dc_miss"],
            dc_index=m["dc_index"],
            dc_way=m["dc_way"],
            rob_num=m["rob_num"],
            prf_num=m["prf_num"],
            ldq_size=m["ldq_size"],
            stq_size=m["stq_size"],
            ftq_size=m["ftq_size"],
            int_alu_ports=m["int_alu_ports"],
            int_muldiv_ports=m["int_muldiv_ports"],
            # Derived for manifest/debug readability.
            alu_simple_ports=max(0, m["int_alu_ports"] - m["int_muldiv_ports"]),
            ld_ports=m["ld_ports"],
            sta_ports=m["sta_ports"],
            std_ports=m["std_ports"],
            br_ports=m["br_ports"],
        )
        ok, _ = validate_cfg(cfg)
        if not ok:
            continue
        picked.append(cfg)
        idx += 1
    return picked


def write_manifest(path: pathlib.Path, rows: List[Tuple[StressConfig, str]]) -> None:
    headers = [
        "name", "status", "cache_profile", "backend_profile", "ftq_profile", "fu_profile", "issue_profile",
        "ic_miss", "dc_miss", "dc_index", "dc_way", "rob_num", "prf_num", "ldq_size", "stq_size",
        "ftq_size", "int_alu_ports", "int_muldiv_ports", "alu_simple_ports",
        "ld_ports", "sta_ports", "std_ports", "br_ports",
    ]
    with path.open("w", encoding="utf-8") as f:
        f.write(",".join(headers) + "\n")
        for cfg, status in rows:
            d = dataclasses.asdict(cfg)
            cols = [str(d[h]) if h in d else status for h in headers]
            cols[1] = status
            f.write(",".join(cols) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Generate stress configs by sampling one profile from each config section."
    )
    ap.add_argument("-n", "--num", type=int, default=10)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--verify", action="store_true", help="build + run dhrystone per config")
    ap.add_argument("--timeout", type=int, default=120, help="run timeout seconds in verify mode")
    ap.add_argument("--out-dir", default=str(OUT_DIR))
    args = ap.parse_args()

    out_dir = pathlib.Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    base = CONFIG_H.read_text(encoding="utf-8")
    backup = base
    rows: List[Tuple[StressConfig, str]] = []

    cfgs = sample_configs(args.num, args.seed)
    if len(cfgs) < args.num:
        print(f"[WARN] requested {args.num}, generated {len(cfgs)} unique valid combinations")

    try:
        for cfg in cfgs:
            text = patch_config(base, cfg)
            (out_dir / f"{cfg.name}.h").write_text(text, encoding="utf-8")

            status = "generated"
            if args.verify:
                CONFIG_H.write_text(text, encoding="utf-8")
                ok, msg = verify(timeout_s=args.timeout)
                status = "PASS" if ok else "FAIL"
                (out_dir / f"{cfg.name}.verify.log").write_text(msg + "\n", encoding="utf-8")

            rows.append((cfg, status))
            print(
                f"[{cfg.name}] {status} "
                f"({cfg.cache_profile}/{cfg.backend_profile}/{cfg.ftq_profile}/{cfg.fu_profile}/{cfg.issue_profile})"
            )
    finally:
        CONFIG_H.write_text(backup, encoding="utf-8")
        if args.verify:
            try:
                run_cmd(["make", "-j"], timeout_s=max(60, args.timeout))
            except Exception:
                pass

    write_manifest(out_dir / "manifest.csv", rows)
    print(f"Wrote: {out_dir}")
    print(f"Wrote: {out_dir / 'manifest.csv'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
