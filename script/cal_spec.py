import os
import re
import glob
import math
from datetime import datetime

# ================= 用户配置区域 =================
CPU_FREQ_GHZ = 1.0
LOG_ROOT_DIR = "./results_restore"
WEIGHTS_DIR = "./bbv"
DEBUG = True
LOG_STATUS_REPORT = "./log_status_report.txt"
PERF_REPORT = "./perf_report.txt"

REF_TIMES = {
    "400.perlbench": 9770, "401.bzip2": 9650, "403.gcc": 8050, "429.mcf": 9120,
    "445.gobmk": 10490, "456.hmmer": 9270, "458.sjeng": 12100, "462.libquantum": 20700,
    "464.h264ref": 22100, "471.omnetpp": 6250, "473.astar": 7020, "483.xalancbmk": 6900
}

INST_COUNTS = {
    "400.perlbench": 0, "401.bzip2": 2232007993519, "403.gcc": 1253567418409, "429.mcf": 293322610121,
    "445.gobmk": 2131355859079, "456.hmmer": 3616124850901, "458.sjeng": 2630677491559, "462.libquantum": 2229437961871,
    "464.h264ref": 5355011654218, "471.omnetpp": 1132608900700, "473.astar": 973000063337, "483.xalancbmk": 1061725890164
}

# 基础正则
REGEX_INST = re.compile(r"instruction\s+num:\s+(\d+)")
REGEX_CYC  = re.compile(r"cycle\s+num:\s+(\d+)")
REGEX_SP_ID = re.compile(r"ckpt_sp(\d+)_")
REGEX_ANSI = re.compile(r"\x1b\[[0-9;]*m")

# [新] 用于 BPU 区域内的强力匹配正则
REGEX_BPU_NUM = re.compile(r"num\s*:\s*(\d+)")
REGEX_BPU_MISPRED = re.compile(r"mispred\s*:\s*(\d+)")

TMA_LABELS = {
    "total_slots": "Total Slots",
    "frontend_bound": "Frontend Bound",
    "fetch_latency": "Fetch Latency",
    "fetch_bandwidth": "Fetch Bandwidth",
    "recovery_total": "Recovery Total",
    "recovery_mispred": "Recovery Mispred",
    "recovery_flush": "Recovery Flush",
    "front_pure": "Front Pure",
    "backend_bound": "Backend Bound",
    "memory_bound": "Memory Bound",
    "l1_bound": "L1 Bound",
    "ext_memory_bound": "Ext Memory Bound",
    "ldq_full": "LDQ Full",
    "stq_full": "STQ Full",
    "core_bound": "Core Bound",
    "iq_bound": "IQ Bound",
    "rob_bound": "ROB Bound",
    "bad_speculation": "Bad Speculation",
    "squash_waste": "Squash Waste",
    "retiring": "Retiring",
}

TMA_IDU_LABELS = {
    "total_slots": "IDU Total Slots",
    "frontend_bound": "IDU Frontend Bound",
    "backend_bound": "IDU Backend Bound",
    "bad_speculation": "IDU Bad Speculation",
    "retiring": "IDU Retiring",
}

def _strip_ansi(s):
    return REGEX_ANSI.sub("", s)

def _extract_last_int(text, label):
    m = re.findall(rf"(?mi)^\s*(?:-\s*)?{re.escape(label)}\s*:\s*(-?\d+)\b", text)
    return int(m[-1]) if m else None

def _extract_last_pct(text, label):
    m = re.findall(
        rf"(?mi)^\s*(?:-\s*)?{re.escape(label)}\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*%",
        text
    )
    return float(m[-1]) if m else None

def _extract_last_pct_any(text, labels):
    for label in labels:
        v = _extract_last_pct(text, label)
        if v is not None:
            return v
    return None

def parse_tma(content):
    total_slots = _extract_last_int(content, TMA_LABELS["total_slots"])
    if total_slots is None:
        return None

    # Core L1 TMA must exist; sub-breakdowns are optional.
    required_aliases = {
        "frontend_bound": ["Frontend Bound"],
        "backend_bound": ["Backend Bound"],
        "bad_speculation": ["Bad Speculation"],
        "retiring": ["Retiring"],
    }
    optional_aliases = {
        "fetch_latency": ["Fetch Latency"],
        "fetch_bandwidth": ["Fetch Bandwidth", "Fetch BW", "Front Pure"],
        "recovery_total": ["Recovery Total"],
        "recovery_mispred": ["Recovery Mispred"],
        "recovery_flush": ["Recovery Flush"],
        "front_pure": ["Front Pure", "Fetch Bandwidth", "Fetch BW"],
        "memory_bound": ["Memory Bound"],
        "l1_bound": ["L1 Bound"],
        "ext_memory_bound": ["Ext Memory Bound"],
        "ldq_full": ["LDQ Full"],
        "stq_full": ["STQ Full"],
        "core_bound": ["Core Bound"],
        "iq_bound": ["IQ Bound"],
        "rob_bound": ["ROB Bound"],
        "squash_waste": ["Squash Waste"],
    }

    tma = {"total_slots": total_slots}
    for k, labels in required_aliases.items():
        v = _extract_last_pct_any(content, labels)
        if v is None:
            return None
        tma[k] = v

    for k, labels in optional_aliases.items():
        tma[k] = _extract_last_pct_any(content, labels)
    return tma

def parse_tma_idu(content):
    total_slots = _extract_last_int(content, TMA_IDU_LABELS["total_slots"])
    if total_slots is None:
        return None

    tma = {"total_slots": total_slots}
    required_aliases = {
        "frontend_bound": [TMA_IDU_LABELS["frontend_bound"]],
        "backend_bound": [TMA_IDU_LABELS["backend_bound"]],
        "bad_speculation": [TMA_IDU_LABELS["bad_speculation"]],
        "retiring": [TMA_IDU_LABELS["retiring"]],
    }
    for k, labels in required_aliases.items():
        v = _extract_last_pct_any(content, labels)
        if v is None:
            return None
        tma[k] = v
    return tma

def dbg(msg):
    if DEBUG:
        print(msg)

def bench_name_aliases(bench_name):
    aliases = [bench_name]

    # Common SPEC folder suffixes
    for suffix in ("_ref", "_base", "_peak", "_test"):
        if bench_name.endswith(suffix):
            aliases.append(bench_name[: -len(suffix)])

    # Fallback: keep prefix before first "_" if present
    if "_" in bench_name:
        aliases.append(bench_name.split("_", 1)[0])
    m = re.match(r"^(\d+\.[A-Za-z0-9]+)", bench_name)
    if m:
        aliases.append(m.group(1))

    # Deduplicate while preserving order
    seen = set()
    ordered = []
    for a in aliases:
        if a and a not in seen:
            seen.add(a)
            ordered.append(a)
    return ordered

def pick_dict_key(candidates, dct):
    for k in candidates:
        if k in dct:
            return k
    return None

def load_weights(bench_name):
    name_candidates = bench_name_aliases(bench_name)
    dbg(f"  [INFO] bench aliases: {name_candidates}")
    candidates = []
    for n in name_candidates:
        candidates.append(os.path.join(WEIGHTS_DIR, f"{n}.weights"))
        candidates.append(os.path.join(WEIGHTS_DIR, f"{n}.pp.weights"))

    weight_file = None
    for c in candidates:
        if os.path.exists(c):
            weight_file = c
            break
    if not weight_file:
        dbg(f"  [WARN] weight file not found for {bench_name}")
        dbg(f"         tried: {candidates[0]}")
        dbg(f"                {candidates[1]}")
        return None

    weights = {}
    try:
        with open(weight_file, 'r') as f:
            for line_idx, line in enumerate(f):
                parts = line.strip().split()
                if not parts: continue
                weight_val = float(parts[0])
                sp_id = int(parts[1]) if len(parts) > 1 else line_idx
                weights[sp_id] = weight_val
    except Exception as e:
        dbg(f"  [WARN] failed to parse weights {weight_file}: {e}")
        return None
    dbg(f"  [INFO] weights loaded: {weight_file}, entries={len(weights)}")
    return weights

def parse_log_robust(filepath, with_reason=False):
    try:
        with open(filepath, 'r') as f:
            lines = [line.strip() for line in f.readlines()]

        content = _strip_ansi("\n".join(lines))
        clean_lines = content.splitlines()

        # 1. Inst/Cycle/Cache
        inst_matches = REGEX_INST.findall(content)
        cyc_matches = REGEX_CYC.findall(content)
        if not inst_matches or not cyc_matches:
            if with_reason: return None, "missing instruction/cycle counters"
            return None

        inst = int(inst_matches[-1])
        cyc = int(cyc_matches[-1])
        if inst == 0:
            if with_reason: return None, "instruction num is 0"
            return None

        dcache_acc = _extract_last_int(content, "dcache access")
        dcache_hit = _extract_last_int(content, "dcache hit")
        dcache_miss = _extract_last_int(content, "dcache miss")
        dcache_acc = int(dcache_acc) if dcache_acc is not None else 0
        dcache_hit = int(dcache_hit) if dcache_hit is not None else 0
        dcache_miss = int(dcache_miss) if dcache_miss is not None else max(0, dcache_acc - dcache_hit)

        icache_acc = _extract_last_int(content, "icache access")
        icache_hit = _extract_last_int(content, "icache hit")
        icache_miss = _extract_last_int(content, "icache miss")
        icache_acc = int(icache_acc) if icache_acc is not None else 0
        icache_hit = int(icache_hit) if icache_hit is not None else 0
        icache_miss = int(icache_miss) if icache_miss is not None else max(0, icache_acc - icache_hit)

        dcache_l2_acc = _extract_last_int(content, "dcache l2 access")
        dcache_l2_miss = _extract_last_int(content, "dcache l2 miss")
        dcache_l2_acc = int(dcache_l2_acc) if dcache_l2_acc is not None else 0
        dcache_l2_miss = int(dcache_l2_miss) if dcache_l2_miss is not None else 0
        icache_l2_acc = _extract_last_int(content, "icache l2 access")
        icache_l2_miss = _extract_last_int(content, "icache l2 miss")
        icache_l2_acc = int(icache_l2_acc) if icache_l2_acc is not None else 0
        icache_l2_miss = int(icache_l2_miss) if icache_l2_miss is not None else 0

        stall_br_id_cycles = _extract_last_int(content, "br id stall cycles")
        stall_preg_cycles = _extract_last_int(content, "preg stall cycles")
        stall_rob_full_cycles = _extract_last_int(content, "rob full stall cycles")
        stall_iq_full_cycles = _extract_last_int(content, "iq full stall cycles")
        stall_ldq_full_cycles = _extract_last_int(content, "ldq full stall cycles")
        stall_stq_full_cycles = _extract_last_int(content, "stq full stall cycles")
        dis2ren_not_ready_cycles = _extract_last_int(content, "dis2ren not ready cycles")
        dis2ren_not_ready_flush_cycles = _extract_last_int(content, "dis2ren flush/stall")
        dis2ren_not_ready_rob_cycles = _extract_last_int(content, "dis2ren rob")
        dis2ren_not_ready_serialize_cycles = _extract_last_int(content, "dis2ren serialize")
        dis2ren_not_ready_dispatch_cycles = _extract_last_int(content, "dis2ren dispatch")
        dis2ren_not_ready_older_cycles = _extract_last_int(content, "dis2ren older")
        dis2ren_not_ready_dispatch_ldq_cycles = _extract_last_int(content, "dispatch ldq")
        dis2ren_not_ready_dispatch_stq_cycles = _extract_last_int(content, "dispatch stq")
        dis2ren_not_ready_dispatch_iq_cycles = _extract_last_int(content, "dispatch iq total")
        dis2ren_not_ready_dispatch_iq_int_cycles = _extract_last_int(content, "iq int")
        dis2ren_not_ready_dispatch_iq_ld_cycles = _extract_last_int(content, "iq ld")
        dis2ren_not_ready_dispatch_iq_sta_cycles = _extract_last_int(content, "iq sta")
        dis2ren_not_ready_dispatch_iq_std_cycles = _extract_last_int(content, "iq std")
        dis2ren_not_ready_dispatch_iq_br_cycles = _extract_last_int(content, "iq br")
        dis2ren_not_ready_dispatch_other_cycles = _extract_last_int(content, "dispatch other")
        ib_blocked_cycles = _extract_last_int(content, "ib blocked cycles")
        ftq_blocked_cycles = _extract_last_int(content, "ftq blocked cycles")

        # =======================================================
        # 2. BPU 解析 (Regex 扫描版)
        # =======================================================
        br_num_total = 0
        br_miss_total = 0

        # A. 找到最后一次出现的 BPU Header
        bpu_start_idx = -1
        for i in range(len(clean_lines) - 1, -1, -1):
            if "*********BPU COUNTER************" in clean_lines[i]:
                bpu_start_idx = i
                break

        if bpu_start_idx != -1:
            # B. 向下扫描
            for i in range(bpu_start_idx + 1, len(clean_lines)):
                line = clean_lines[i]

                # C. 遇到下一个分隔符就停止
                if "*********" in line:
                    break

                # D. 使用正则查找 "num : 数字"
                # 不管这一行开头是什么，只要包含这个模式就提取
                n_match = REGEX_BPU_NUM.search(line)
                if n_match:
                    br_num_total += int(n_match.group(1))

                m_match = REGEX_BPU_MISPRED.search(line)
                if m_match:
                    br_miss_total += int(m_match.group(1))

        tma = parse_tma(content)
        tma_idu = parse_tma_idu(content)

        data = {
            "inst": inst, "cyc": cyc, "cpi": cyc / inst,
            "dcache_hit": dcache_hit, "dcache_acc": dcache_acc, "dcache_miss": dcache_miss,
            "icache_hit": icache_hit, "icache_acc": icache_acc, "icache_miss": icache_miss,
            "dcache_l2_acc": dcache_l2_acc, "dcache_l2_miss": dcache_l2_miss,
            "icache_l2_acc": icache_l2_acc, "icache_l2_miss": icache_l2_miss,
            "br_num": br_num_total, "br_miss": br_miss_total,
            "stall_br_id_cycles": stall_br_id_cycles or 0,
            "stall_preg_cycles": stall_preg_cycles or 0,
            "stall_rob_full_cycles": stall_rob_full_cycles or 0,
            "stall_iq_full_cycles": stall_iq_full_cycles or 0,
            "stall_ldq_full_cycles": stall_ldq_full_cycles or 0,
            "stall_stq_full_cycles": stall_stq_full_cycles or 0,
            "dis2ren_not_ready_cycles": dis2ren_not_ready_cycles or 0,
            "dis2ren_not_ready_flush_cycles": dis2ren_not_ready_flush_cycles or 0,
            "dis2ren_not_ready_rob_cycles": dis2ren_not_ready_rob_cycles or 0,
            "dis2ren_not_ready_serialize_cycles": dis2ren_not_ready_serialize_cycles or 0,
            "dis2ren_not_ready_dispatch_cycles": dis2ren_not_ready_dispatch_cycles or 0,
            "dis2ren_not_ready_older_cycles": dis2ren_not_ready_older_cycles or 0,
            "dis2ren_not_ready_dispatch_ldq_cycles": dis2ren_not_ready_dispatch_ldq_cycles or 0,
            "dis2ren_not_ready_dispatch_stq_cycles": dis2ren_not_ready_dispatch_stq_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_cycles": dis2ren_not_ready_dispatch_iq_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_int_cycles": dis2ren_not_ready_dispatch_iq_int_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_ld_cycles": dis2ren_not_ready_dispatch_iq_ld_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_sta_cycles": dis2ren_not_ready_dispatch_iq_sta_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_std_cycles": dis2ren_not_ready_dispatch_iq_std_cycles or 0,
            "dis2ren_not_ready_dispatch_iq_br_cycles": dis2ren_not_ready_dispatch_iq_br_cycles or 0,
            "dis2ren_not_ready_dispatch_other_cycles": dis2ren_not_ready_dispatch_other_cycles or 0,
            "ib_blocked_cycles": ib_blocked_cycles or 0,
            "ftq_blocked_cycles": ftq_blocked_cycles or 0,
            "tma": tma,
            "tma_idu": tma_idu,
        }
        if with_reason: return data, None
        return data

    except Exception as e:
        if with_reason: return None, f"parse exception: {e}"
        print(f"Error parsing {filepath}: {e}")
        return None

def check_log_success(filepath):
    try:
        with open(filepath, "r") as f:
            content = _strip_ansi(f.read())
        return ("Success!!!!" in content), None
    except Exception as e:
        return False, f"open/read failed: {e}"

def print_log_list(title, files):
    print(f"  {title} ({len(files)}):")
    if not files:
        print("    - None")
        return
    for fp in files:
        print(f"    - {os.path.basename(fp)}")

def process_benchmark(bench_path):
    status_lines = []
    perf_lines = []

    def s(msg):
        status_lines.append(msg)

    def p(msg):
        perf_lines.append(msg)

    bench_name = os.path.basename(bench_path)
    name_candidates = bench_name_aliases(bench_name)
    bench_key = pick_dict_key(name_candidates, INST_COUNTS)
    if not bench_key:
        bench_key = pick_dict_key(name_candidates, REF_TIMES)
    if not bench_key:
        bench_key = bench_name

    if bench_key != bench_name:
        s(f"[INFO] bench name normalized: {bench_name} -> {bench_key}")

    weights_map = load_weights(bench_name)
    if not weights_map:
        s(f"[SKIP] {bench_name}: no usable weight file")
        return 0.0, status_lines, perf_lines

    s("")
    s(f"Processing: {bench_name}")
    log_files = glob.glob(os.path.join(bench_path, "*.log"))
    s(f"  [INFO] found log files: {len(log_files)}")
    if len(log_files) == 0:
        s("  [WARN] benchmark dir has no .log files")
        return 0.0, status_lines, perf_lines

    # Phase 1: classify logs by filename/weight/success marker
    useful_logs = []
    error_logs = []
    skipped_no_sp = 0
    skipped_no_weight = 0
    for log_file in log_files:
        filename = os.path.basename(log_file)
        sp_match = REGEX_SP_ID.search(filename)
        if not sp_match:
            skipped_no_sp += 1
            error_logs.append(log_file)
            continue

        sp_id = int(sp_match.group(1))
        if sp_id not in weights_map:
            skipped_no_weight += 1
            error_logs.append(log_file)
            continue

        ok, reason = check_log_success(log_file)
        if not ok:
            if reason:
                dbg(f"  [WARN] {filename}: {reason}")
            error_logs.append(log_file)
            continue
        useful_logs.append(log_file)

    s(f"  Useful logs (with Success!!!!) ({len(useful_logs)}):")
    if useful_logs:
        for fp in useful_logs:
            s(f"    - {os.path.basename(fp)}")
    else:
        s("    - None")

    s(f"  Error logs (missing Success!!!! or unmatched) ({len(error_logs)}):")
    if error_logs:
        for fp in error_logs:
            s(f"    - {os.path.basename(fp)}")
    else:
        s("    - None")

    w_cpi_sum = 0.0; w_weight_sum = 0.0; w_inst_sum = 0.0; w_cyc_sum = 0.0
    w_dcache_hit = 0.0; w_dcache_acc = 0.0; w_dcache_miss = 0.0
    w_icache_hit = 0.0; w_icache_acc = 0.0; w_icache_miss = 0.0
    w_dcache_l2_hit = 0.0; w_dcache_l2_acc = 0.0; w_dcache_l2_miss = 0.0
    w_icache_l2_hit = 0.0; w_icache_l2_acc = 0.0; w_icache_l2_miss = 0.0
    w_br_hit = 0.0; w_br_total = 0.0; w_br_miss = 0.0
    w_stall_br_id = 0.0; w_stall_preg = 0.0; w_stall_rob_full = 0.0
    w_stall_iq_full = 0.0; w_stall_ldq_full = 0.0; w_stall_stq_full = 0.0
    w_dis2ren_not_ready = 0.0
    w_dis2ren_not_ready_flush = 0.0
    w_dis2ren_not_ready_rob = 0.0
    w_dis2ren_not_ready_serialize = 0.0
    w_dis2ren_not_ready_dispatch = 0.0
    w_dis2ren_not_ready_older = 0.0
    w_dis2ren_not_ready_dispatch_ldq = 0.0
    w_dis2ren_not_ready_dispatch_stq = 0.0
    w_dis2ren_not_ready_dispatch_iq = 0.0
    w_dis2ren_not_ready_dispatch_iq_int = 0.0
    w_dis2ren_not_ready_dispatch_iq_ld = 0.0
    w_dis2ren_not_ready_dispatch_iq_sta = 0.0
    w_dis2ren_not_ready_dispatch_iq_std = 0.0
    w_dis2ren_not_ready_dispatch_iq_br = 0.0
    w_dis2ren_not_ready_dispatch_other = 0.0
    w_ib_blocked = 0.0; w_ftq_blocked = 0.0

    w_tma_slots = 0.0
    tma_slot_sums = {
        "frontend_bound": 0.0,
        "fetch_latency": 0.0,
        "fetch_bandwidth": 0.0,
        "recovery_total": 0.0,
        "recovery_mispred": 0.0,
        "recovery_flush": 0.0,
        "front_pure": 0.0,
        "backend_bound": 0.0,
        "memory_bound": 0.0,
        "l1_bound": 0.0,
        "ext_memory_bound": 0.0,
        "ldq_full": 0.0,
        "stq_full": 0.0,
        "core_bound": 0.0,
        "iq_bound": 0.0,
        "rob_bound": 0.0,
        "bad_speculation": 0.0,
        "squash_waste": 0.0,
        "retiring": 0.0,
    }
    tma_slot_denom = {k: 0.0 for k in tma_slot_sums.keys()}

    w_tma_idu_slots = 0.0
    tma_idu_slot_sums = {
        "frontend_bound": 0.0,
        "backend_bound": 0.0,
        "bad_speculation": 0.0,
        "retiring": 0.0,
    }

    valid_files = 0
    skipped_bad_log = 0
    bad_reasons = {}
    for log_file in useful_logs:
        filename = os.path.basename(log_file)
        sp_match = REGEX_SP_ID.search(filename)
        if not sp_match:
            skipped_bad_log += 1
            bad_reasons["missing sp id after phase-1 filter"] = bad_reasons.get("missing sp id after phase-1 filter", 0) + 1
            continue
        sp_id = int(sp_match.group(1))

        if sp_id not in weights_map:
            skipped_bad_log += 1
            bad_reasons["sp id not in weights after phase-1 filter"] = bad_reasons.get("sp id not in weights after phase-1 filter", 0) + 1
            continue
        weight = weights_map[sp_id]

        data, reason = parse_log_robust(log_file, with_reason=True)
        if not data:
            skipped_bad_log += 1
            bad_reasons[reason] = bad_reasons.get(reason, 0) + 1
            continue

        valid_files += 1

        w_cpi_sum += data["cpi"] * weight
        w_weight_sum += weight
        w_inst_sum += data["inst"] * weight
        w_cyc_sum += data["cyc"] * weight

        w_dcache_hit += data["dcache_hit"] * weight
        w_dcache_acc += data["dcache_acc"] * weight
        w_dcache_miss += data["dcache_miss"] * weight
        w_icache_hit += data["icache_hit"] * weight
        w_icache_acc += data["icache_acc"] * weight
        w_icache_miss += data["icache_miss"] * weight
        w_dcache_l2_acc += data["dcache_l2_acc"] * weight
        w_dcache_l2_miss += data["dcache_l2_miss"] * weight
        w_dcache_l2_hit += (data["dcache_l2_acc"] - data["dcache_l2_miss"]) * weight
        w_icache_l2_acc += data["icache_l2_acc"] * weight
        w_icache_l2_miss += data["icache_l2_miss"] * weight
        w_icache_l2_hit += (data["icache_l2_acc"] - data["icache_l2_miss"]) * weight

        br_correct = data["br_num"] - data["br_miss"]
        w_br_hit += br_correct * weight
        w_br_total += data["br_num"] * weight
        w_br_miss += data["br_miss"] * weight

        w_stall_br_id += data["stall_br_id_cycles"] * weight
        w_stall_preg += data["stall_preg_cycles"] * weight
        w_stall_rob_full += data["stall_rob_full_cycles"] * weight
        w_stall_iq_full += data["stall_iq_full_cycles"] * weight
        w_stall_ldq_full += data["stall_ldq_full_cycles"] * weight
        w_stall_stq_full += data["stall_stq_full_cycles"] * weight
        w_dis2ren_not_ready += data["dis2ren_not_ready_cycles"] * weight
        w_dis2ren_not_ready_flush += data["dis2ren_not_ready_flush_cycles"] * weight
        w_dis2ren_not_ready_rob += data["dis2ren_not_ready_rob_cycles"] * weight
        w_dis2ren_not_ready_serialize += data["dis2ren_not_ready_serialize_cycles"] * weight
        w_dis2ren_not_ready_dispatch += data["dis2ren_not_ready_dispatch_cycles"] * weight
        w_dis2ren_not_ready_older += data["dis2ren_not_ready_older_cycles"] * weight
        w_dis2ren_not_ready_dispatch_ldq += data["dis2ren_not_ready_dispatch_ldq_cycles"] * weight
        w_dis2ren_not_ready_dispatch_stq += data["dis2ren_not_ready_dispatch_stq_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq += data["dis2ren_not_ready_dispatch_iq_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq_int += data["dis2ren_not_ready_dispatch_iq_int_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq_ld += data["dis2ren_not_ready_dispatch_iq_ld_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq_sta += data["dis2ren_not_ready_dispatch_iq_sta_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq_std += data["dis2ren_not_ready_dispatch_iq_std_cycles"] * weight
        w_dis2ren_not_ready_dispatch_iq_br += data["dis2ren_not_ready_dispatch_iq_br_cycles"] * weight
        w_dis2ren_not_ready_dispatch_other += data["dis2ren_not_ready_dispatch_other_cycles"] * weight
        w_ib_blocked += data["ib_blocked_cycles"] * weight
        w_ftq_blocked += data["ftq_blocked_cycles"] * weight

        # TMA: 用 Total Slots * 百分比恢复为“槽位数”，再进行 simpoint 加权汇总
        if data["tma"] and data["tma"]["total_slots"] > 0:
            tma_total = data["tma"]["total_slots"] * weight
            w_tma_slots += tma_total
            for k in tma_slot_sums.keys():
                v = data["tma"].get(k, None)
                if v is None:
                    continue
                tma_slot_sums[k] += tma_total * (v / 100.0)
                tma_slot_denom[k] += tma_total

        if data["tma_idu"] and data["tma_idu"]["total_slots"] > 0:
            tma_idu_total = data["tma_idu"]["total_slots"] * weight
            w_tma_idu_slots += tma_idu_total
            for k in tma_idu_slot_sums.keys():
                v = data["tma_idu"].get(k, None)
                if v is None:
                    continue
                tma_idu_slot_sums[k] += tma_idu_total * (v / 100.0)

    if valid_files == 0:
        s("  -> No valid logs found.")
        s(f"  [DIAG] skip(no sp id in filename): {skipped_no_sp}")
        s(f"  [DIAG] skip(sp id not in weights): {skipped_no_weight}")
        s(f"  [DIAG] skip(parse failed):         {skipped_bad_log}")
        for reason, cnt in sorted(bad_reasons.items(), key=lambda x: -x[1]):
            s(f"  [DIAG]   - {reason}: {cnt}")
        return 0.0, status_lines, perf_lines

    final_cpi = (w_cpi_sum / w_weight_sum) if w_weight_sum > 0 else 0.0
    final_ipc = 1.0 / final_cpi if final_cpi > 0 else 0

    dcache_rate = (w_dcache_hit / w_dcache_acc * 100) if w_dcache_acc > 0 else 0
    dcache_mpki = (w_dcache_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0
    icache_rate = (w_icache_hit / w_icache_acc * 100) if w_icache_acc > 0 else 0
    icache_mpki = (w_icache_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0
    dcache_l2_rate = (w_dcache_l2_hit / w_dcache_l2_acc * 100) if w_dcache_l2_acc > 0 else 0
    dcache_l2_mpki = (w_dcache_l2_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0
    icache_l2_rate = (w_icache_l2_hit / w_icache_l2_acc * 100) if w_icache_l2_acc > 0 else 0
    icache_l2_mpki = (w_icache_l2_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0

    br_acc = (w_br_hit / w_br_total * 100) if w_br_total > 0 else 0
    br_mpki = (w_br_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0

    total_insts = INST_COUNTS.get(bench_key, 0)
    score = 0.0

    p("-" * 65)
    p(f"Benchmark:          {bench_name} (key={bench_key})")
    p(f"Files:              {valid_files}")
    p(f"Effective Weight:   {w_weight_sum:.4f}")
    p(f"Skip(no sp id):     {skipped_no_sp}")
    p(f"Skip(no weight):    {skipped_no_weight}")
    p(f"Skip(parse failed): {skipped_bad_log}")
    for reason, cnt in sorted(bad_reasons.items(), key=lambda x: -x[1]):
        p(f"Parse fail detail:  {reason} ({cnt})")
    if w_weight_sum < 0.9999:
        p(f"[WARN] Effective weight sum is {w_weight_sum:.4f} (< 1.0).")
        p("[WARN] CPI/IPC is renormalized by available weights.")

    p("")
    p("[Core]")
    p(f"Weighted Insts:     {w_inst_sum:.2f}")
    p(f"Weighted Cycles:    {w_cyc_sum:.2f}")
    p(f"Weighted IPC:       {final_ipc:.4f}")
    p(f"Weighted CPI:       {final_cpi:.4f}")

    p("")
    p("[Cache]")
    p(f"DCache Hit Rate:    {dcache_rate:.2f} %")
    p(f"DCache MPKI:        {dcache_mpki:.2f}")
    if w_dcache_l2_acc > 0:
        p(f"DCache L2 Hit Rate: {dcache_l2_rate:.2f} %")
        p(f"DCache L2 MPKI:     {dcache_l2_mpki:.2f}")
    else:
        p("DCache L2 Hit Rate: N/A (no DCache L2 accesses parsed)")

    p(f"ICache Hit Rate:    {icache_rate:.2f} %")
    p(f"ICache MPKI:        {icache_mpki:.2f}")
    if w_icache_l2_acc > 0:
        p(f"ICache L2 Hit Rate: {icache_l2_rate:.2f} %")
        p(f"ICache L2 MPKI:     {icache_l2_mpki:.2f}")
    else:
        p("ICache L2 Hit Rate: N/A (no ICache L2 accesses parsed)")

    p("")
    p("[BPU]")
    if w_br_total > 0:
        p(f"Branch Accuracy:    {br_acc:.2f} %")
        p(f"Branch MPKI:        {br_mpki:.2f}")
    else:
        p("Branch Accuracy:    N/A (Parsed 0 branches)")

    p("")
    p("[Resource]")
    if w_cyc_sum > 0:
        p("Resource Stall (Weighted, cycles):")
        p(f"  BR ID Stall:      {w_stall_br_id:.2f}")
        p(f"  Preg Stall:       {w_stall_preg:.2f}")
        p(f"  ROB Full Stall:   {w_stall_rob_full:.2f}")
        p(f"  IQ Full Stall:    {w_stall_iq_full:.2f}")
        p(f"  LDQ Full Stall:   {w_stall_ldq_full:.2f}")
        p(f"  STQ Full Stall:   {w_stall_stq_full:.2f}")
        p(f"  Dis2Ren !Ready:   {w_dis2ren_not_ready:.2f}")
        p(f"    - Flush/Stall:  {w_dis2ren_not_ready_flush:.2f}")
        p(f"    - ROB:          {w_dis2ren_not_ready_rob:.2f}")
        p(f"    - Serialize:    {w_dis2ren_not_ready_serialize:.2f}")
        p(f"    - Dispatch:     {w_dis2ren_not_ready_dispatch:.2f}")
        p(f"    - Older:        {w_dis2ren_not_ready_older:.2f}")
        p(f"      - LDQ:        {w_dis2ren_not_ready_dispatch_ldq:.2f}")
        p(f"      - STQ:        {w_dis2ren_not_ready_dispatch_stq:.2f}")
        p(f"      - IQ Total:   {w_dis2ren_not_ready_dispatch_iq:.2f}")
        p(f"        - IQ INT:   {w_dis2ren_not_ready_dispatch_iq_int:.2f}")
        p(f"        - IQ LD:    {w_dis2ren_not_ready_dispatch_iq_ld:.2f}")
        p(f"        - IQ STA:   {w_dis2ren_not_ready_dispatch_iq_sta:.2f}")
        p(f"        - IQ STD:   {w_dis2ren_not_ready_dispatch_iq_std:.2f}")
        p(f"        - IQ BR:    {w_dis2ren_not_ready_dispatch_iq_br:.2f}")
        p(f"      - Other:      {w_dis2ren_not_ready_dispatch_other:.2f}")
        p("Frontend Block (Weighted, cycles):")
        p(f"  IB Blocked:       {w_ib_blocked:.2f}")
        p(f"  FTQ Blocked:      {w_ftq_blocked:.2f}")
    else:
        p("Resource Stall:     N/A (weighted cycles is 0)")

    p("")
    p("[TMA]")
    if w_tma_slots > 0:
        p("TMA (Weighted):")
        p(f"  Frontend Bound:   {tma_slot_sums['frontend_bound'] / w_tma_slots * 100:.2f} %")
        if tma_slot_denom["recovery_total"] > 0:
            p(f"    Recovery Total: {tma_slot_sums['recovery_total'] / tma_slot_denom['recovery_total'] * 100:.2f} %")
        if tma_slot_denom["recovery_mispred"] > 0:
            p(f"    Recovery Misp:  {tma_slot_sums['recovery_mispred'] / tma_slot_denom['recovery_mispred'] * 100:.2f} %")
        if tma_slot_denom["recovery_flush"] > 0:
            p(f"    Recovery Flush: {tma_slot_sums['recovery_flush'] / tma_slot_denom['recovery_flush'] * 100:.2f} %")
        if tma_slot_denom["front_pure"] > 0:
            p(f"    Front Pure:     {tma_slot_sums['front_pure'] / tma_slot_denom['front_pure'] * 100:.2f} %")
        if tma_slot_denom["fetch_latency"] > 0:
            p(f"    Fetch Latency:  {tma_slot_sums['fetch_latency'] / tma_slot_denom['fetch_latency'] * 100:.2f} %")
        if tma_slot_denom["fetch_bandwidth"] > 0:
            p(f"    Fetch BW:       {tma_slot_sums['fetch_bandwidth'] / tma_slot_denom['fetch_bandwidth'] * 100:.2f} %")
        p(f"  Backend Bound:    {tma_slot_sums['backend_bound'] / w_tma_slots * 100:.2f} %")
        if tma_slot_denom["memory_bound"] > 0:
            p(f"    Memory Bound:   {tma_slot_sums['memory_bound'] / tma_slot_denom['memory_bound'] * 100:.2f} %")
        if tma_slot_denom["ldq_full"] > 0:
            p(f"      LDQ Full:     {tma_slot_sums['ldq_full'] / tma_slot_denom['ldq_full'] * 100:.2f} %")
        if tma_slot_denom["stq_full"] > 0:
            p(f"      STQ Full:     {tma_slot_sums['stq_full'] / tma_slot_denom['stq_full'] * 100:.2f} %")
        if tma_slot_denom["l1_bound"] > 0:
            p(f"      L1 Bound:     {tma_slot_sums['l1_bound'] / tma_slot_denom['l1_bound'] * 100:.2f} %")
        if tma_slot_denom["ext_memory_bound"] > 0:
            p(f"      Ext Mem Bound:{tma_slot_sums['ext_memory_bound'] / tma_slot_denom['ext_memory_bound'] * 100:.2f} %")
        if tma_slot_denom["core_bound"] > 0:
            p(f"    Core Bound:     {tma_slot_sums['core_bound'] / tma_slot_denom['core_bound'] * 100:.2f} %")
        if tma_slot_denom["iq_bound"] > 0:
            p(f"      IQ Bound:     {tma_slot_sums['iq_bound'] / tma_slot_denom['iq_bound'] * 100:.2f} %")
        if tma_slot_denom["rob_bound"] > 0:
            p(f"      ROB Bound:    {tma_slot_sums['rob_bound'] / tma_slot_denom['rob_bound'] * 100:.2f} %")
        p(f"  Bad Speculation:  {tma_slot_sums['bad_speculation'] / w_tma_slots * 100:.2f} %")
        if tma_slot_denom["squash_waste"] > 0:
            p(f"    Squash Waste:   {tma_slot_sums['squash_waste'] / tma_slot_denom['squash_waste'] * 100:.2f} %")
        p(f"  Retiring:         {tma_slot_sums['retiring'] / w_tma_slots * 100:.2f} %")
    else:
        p("TMA (Weighted):     N/A (Top-Down section not found)")

    if w_tma_idu_slots > 0:
        p("TMA-IDU (Weighted):")
        p(f"  Frontend Bound:   {tma_idu_slot_sums['frontend_bound'] / w_tma_idu_slots * 100:.2f} %")
        p(f"  Backend Bound:    {tma_idu_slot_sums['backend_bound'] / w_tma_idu_slots * 100:.2f} %")
        p(f"  Bad Speculation:  {tma_idu_slot_sums['bad_speculation'] / w_tma_idu_slots * 100:.2f} %")
        p(f"  Retiring:         {tma_idu_slot_sums['retiring'] / w_tma_idu_slots * 100:.2f} %")
    else:
        p("TMA-IDU (Weighted): N/A (IDU Top-Down section not found)")

    p("")
    p("[SPEC]")
    if total_insts > 0:
        pred_cycles = total_insts * final_cpi
        pred_time = pred_cycles / (CPU_FREQ_GHZ * 1e9)
        ref_time = REF_TIMES.get(bench_key, 0)
        if pred_time > 0 and ref_time > 0:
            score = ref_time / pred_time
        elif ref_time <= 0:
            p(f"[WARN] SPEC Ratio is 0 because REF_TIMES has no key: {bench_key}")
        p(f"SPEC Ratio:         {score:.2f}")
    else:
        p(f"[WARN] SPEC Ratio is 0 because INST_COUNTS[{bench_key}] is missing/0.")
    p("=" * 65)
    return score, status_lines, perf_lines

def main():
    status_report_lines = []
    perf_report_lines = []
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    status_report_lines.append(f"Log Status Report @ {ts}")
    status_report_lines.append(f"LOG_ROOT_DIR = {os.path.abspath(LOG_ROOT_DIR)}")
    status_report_lines.append(f"WEIGHTS_DIR  = {os.path.abspath(WEIGHTS_DIR)}")
    status_report_lines.append("")
    perf_report_lines.append(f"Performance Report @ {ts}")
    perf_report_lines.append(f"LOG_ROOT_DIR = {os.path.abspath(LOG_ROOT_DIR)}")
    perf_report_lines.append("")

    if not os.path.exists(LOG_ROOT_DIR):
        status_report_lines.append("Error: Log dir not found.")
        with open(LOG_STATUS_REPORT, "w") as f:
            f.write("\n".join(status_report_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return
    if not os.path.exists(WEIGHTS_DIR):
        status_report_lines.append("Error: Weights dir not found.")
        with open(LOG_STATUS_REPORT, "w") as f:
            f.write("\n".join(status_report_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return
    bench_dirs = sorted([d for d in glob.glob(os.path.join(LOG_ROOT_DIR, "*")) if os.path.isdir(d)])
    status_report_lines.append(f"[INFO] benchmark dirs found: {len(bench_dirs)}")
    if not bench_dirs:
        status_report_lines.append("[WARN] No benchmark directories under LOG_ROOT_DIR.")
        with open(LOG_STATUS_REPORT, "w") as f:
            f.write("\n".join(status_report_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return
    scores = []
    for d in bench_dirs:
        s, status_lines, perf_lines = process_benchmark(d)
        status_report_lines.extend(status_lines)
        perf_report_lines.extend(perf_lines)
        if s and s > 0: scores.append(s)
    if scores:
        import functools, operator
        geomean = (functools.reduce(operator.mul, scores, 1)) ** (1.0 / len(scores))
        perf_report_lines.append("")
        perf_report_lines.append(f"Final Estimated SPECint2006: {geomean:.2f}")
    else:
        perf_report_lines.append("")
        perf_report_lines.append("[WARN] No benchmark produced a valid SPEC ratio.")

    with open(LOG_STATUS_REPORT, "w") as f:
        f.write("\n".join(status_report_lines) + "\n")
    with open(PERF_REPORT, "w") as f:
        f.write("\n".join(perf_report_lines) + "\n")

    print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
    print(f"Wrote perf report:   {os.path.abspath(PERF_REPORT)}")

if __name__ == "__main__":
    main()
