import os
import re
import glob
import math
from datetime import datetime

# ================= 用户配置区域 =================
CPU_FREQ_GHZ = 1.0
LOG_ROOT_DIR = "./results_restore-fal-scl-256"
WEIGHTS_DIR = "./rv32imab_bbv"
DEBUG = True
LOG_STATUS_REPORT = "./res/log_status_report-fal-scl-256.txt"
PERF_REPORT = "./res/perf_report-fal-scl-256.txt"

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
REGEX_CACHE_HIT = re.compile(r"(?<!i)cache\s+hit\s+:\s+(\d+)")
REGEX_CACHE_ACC = re.compile(r"(?<!i)cache\s+access\s+:\s+(\d+)")
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
    "backend_bound": "Backend Bound",
    "memory_bound": "Memory Bound",
    "l1_bound": "L1 Bound",
    "ext_memory_bound": "Ext Memory Bound",
    "core_bound": "Core Bound",
    "bad_speculation": "Bad Speculation",
    "squash_waste": "Squash Waste",
    "retiring": "Retiring",
}

def _strip_ansi(s):
    return REGEX_ANSI.sub("", s)

def _extract_last_int(text, label):
    m = re.findall(rf"{re.escape(label)}\s*:\s*(\d+)", text)
    return int(m[-1]) if m else None

def _extract_last_pct(text, label):
    m = re.findall(rf"{re.escape(label)}\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*%", text)
    return float(m[-1]) if m else None

def parse_tma(content):
    total_slots = _extract_last_int(content, TMA_LABELS["total_slots"])
    if total_slots is None:
        return None

    tma = {"total_slots": total_slots}
    required_keys = {"frontend_bound", "backend_bound", "bad_speculation", "retiring"}
    for k, label in TMA_LABELS.items():
        if k == "total_slots":
            continue
        v = _extract_last_pct(content, label)
        if v is None:
            if k in required_keys:
                return None
            tma[k] = 0.0
            continue
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

        c_hit_matches = REGEX_CACHE_HIT.findall(content)
        c_acc_matches = REGEX_CACHE_ACC.findall(content)
        cache_hit = int(c_hit_matches[-1]) if c_hit_matches else 0
        cache_acc = int(c_acc_matches[-1]) if c_acc_matches else 0

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

        data = {
            "inst": inst, "cyc": cyc, "cpi": cyc / inst,
            "cache_hit": cache_hit, "cache_acc": cache_acc,
            "br_num": br_num_total, "br_miss": br_miss_total,
            "tma": tma,
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

    w_cpi_sum = 0.0; w_weight_sum = 0.0; w_inst_sum = 0.0
    w_cache_hit = 0.0; w_cache_acc = 0.0; w_cache_miss = 0.0
    w_br_hit = 0.0; w_br_total = 0.0; w_br_miss = 0.0

    w_tma_slots = 0.0
    tma_slot_sums = {
        "frontend_bound": 0.0,
        "fetch_latency": 0.0,
        "fetch_bandwidth": 0.0,
        "backend_bound": 0.0,
        "memory_bound": 0.0,
        "l1_bound": 0.0,
        "ext_memory_bound": 0.0,
        "core_bound": 0.0,
        "bad_speculation": 0.0,
        "squash_waste": 0.0,
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

        w_cache_hit += data["cache_hit"] * weight
        w_cache_acc += data["cache_acc"] * weight
        w_cache_miss += (data["cache_acc"] - data["cache_hit"]) * weight

        br_correct = data["br_num"] - data["br_miss"]
        w_br_hit += br_correct * weight
        w_br_total += data["br_num"] * weight
        w_br_miss += data["br_miss"] * weight

        # TMA: 用 Total Slots * 百分比恢复为“槽位数”，再进行 simpoint 加权汇总
        if data["tma"] and data["tma"]["total_slots"] > 0:
            tma_total = data["tma"]["total_slots"] * weight
            w_tma_slots += tma_total
            for k in tma_slot_sums.keys():
                tma_slot_sums[k] += tma_total * (data["tma"][k] / 100.0)

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

    cache_rate = (w_cache_hit / w_cache_acc * 100) if w_cache_acc > 0 else 0
    cache_mpki = (w_cache_miss / w_inst_sum * 1000) if w_inst_sum > 0 else 0

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
    p("-" * 65)
    p(f"Weighted IPC:       {final_ipc:.4f}")
    p(f"Weighted CPI:       {final_cpi:.4f}")
    p(f"Cache Hit Rate:     {cache_rate:.2f} %")
    p(f"Cache MPKI:         {cache_mpki:.2f}")

    if w_br_total > 0:
        p(f"Branch Accuracy:    {br_acc:.2f} %")
        p(f"Branch MPKI:        {br_mpki:.2f}")
    else:
        p("Branch Accuracy:    N/A (Parsed 0 branches)")

    if w_tma_slots > 0:
        p("TMA (Weighted):")
        p(f"  Frontend Bound:   {tma_slot_sums['frontend_bound'] / w_tma_slots * 100:.2f} %")
        p(f"    Fetch Latency:  {tma_slot_sums['fetch_latency'] / w_tma_slots * 100:.2f} %")
        p(f"    Fetch BW:       {tma_slot_sums['fetch_bandwidth'] / w_tma_slots * 100:.2f} %")
        p(f"  Backend Bound:    {tma_slot_sums['backend_bound'] / w_tma_slots * 100:.2f} %")
        p(f"    Memory Bound:   {tma_slot_sums['memory_bound'] / w_tma_slots * 100:.2f} %")
        p(f"      L1 Bound:     {tma_slot_sums['l1_bound'] / w_tma_slots * 100:.2f} %")
        p(f"      Ext Mem Bound:{tma_slot_sums['ext_memory_bound'] / w_tma_slots * 100:.2f} %")
        p(f"    Core Bound:     {tma_slot_sums['core_bound'] / w_tma_slots * 100:.2f} %")
        p(f"  Bad Speculation:  {tma_slot_sums['bad_speculation'] / w_tma_slots * 100:.2f} %")
        p(f"    Squash Waste:   {tma_slot_sums['squash_waste'] / w_tma_slots * 100:.2f} %")
        p(f"  Retiring:         {tma_slot_sums['retiring'] / w_tma_slots * 100:.2f} %")
    else:
        p("TMA (Weighted):     N/A (Top-Down section not found)")

    if total_insts > 0:
        pred_cycles = total_insts * final_cpi
        pred_time = pred_cycles / (CPU_FREQ_GHZ * 1e9)
        ref_time = REF_TIMES.get(bench_key, 0)
        if pred_time > 0 and ref_time > 0: score = ref_time / pred_time
        p(f"SPEC Ratio:         {score:.2f}")
    else:
        p("[Info] Set INST_COUNTS in script to see SPEC Score.")
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
