import os
import re
import glob
import ast
from datetime import datetime

# ================= 用户配置区域 =================
CPU_FREQ_GHZ = float(os.environ.get("CPU_FREQ_GHZ", "1.0"))
LOG_ROOT_DIR = os.environ.get("LOG_ROOT_DIR", "./results_restore_456")
WEIGHTS_DIR = os.environ.get(
    "WEIGHTS_DIR", "/share/personal/S/houruyao/simpoint/rv32imab_bbv_1gb_ram"
)
DEBUG = os.environ.get("DEBUG", "1") not in ("0", "false", "False")
LOG_STATUS_REPORT = LOG_ROOT_DIR + "/log_status_report.txt"
PERF_REPORT = LOG_ROOT_DIR + "/perf_report.txt"
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

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
    "400.perlbench": 2063973377122,
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

# 基础正则
REGEX_INST = re.compile(r"instruction\s+num:\s+(\d+)")
REGEX_CYC = re.compile(r"cycle\s+num:\s+(\d+)")
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

TMA_SLOT_KEYS = tuple(k for k in TMA_LABELS.keys() if k != "total_slots")

CACHE_COUNTER_LABELS = {
    "l1i_access": "L1I access",
    "l1i_hit": "L1I hit",
    "l1i_miss": "L1I miss",
    "l1d_req_initial": "L1D_REQ_INITIAL",
    "l1d_miss_mshr_alloc": "L1D_MISS_MSHR_ALLOC",
    "llc_read_access": "llc read access",
    "llc_read_hit": "llc read hit",
    "llc_read_miss": "llc read miss",
}

CACHE_TRIPLET_LABELS = {
    "llc_l1i": "llc l1i a/h/m",
    "llc_l1d": "llc l1d a/h/m",
}

WEIGHTED_CACHE_KEYS = (
    "l1i_access",
    "l1i_hit",
    "l1i_miss",
    "l1d_req_initial",
    "l1d_miss_mshr_alloc",
    "llc_read_access",
    "llc_read_hit",
    "llc_read_miss",
    "llc_icache_access",
    "llc_icache_hit",
    "llc_icache_miss",
    "llc_dcache_access",
    "llc_dcache_hit",
    "llc_dcache_miss",
)

LATENCY_METRIC_KEYS = (
    "l1d_miss_penalty",
    "l1d_axi_read_latency",
    "l1d_axi_write_latency",
    "l1d_mem_inst_latency",
    "l1i_miss_penalty",
    "l1i_axi_read_latency",
)

LATENCY_METRIC_LABELS = {
    "l1d_miss_penalty": "L1D Avg Miss Penalty",
    "l1d_axi_read_latency": "L1D Avg AXI Read",
    "l1d_axi_write_latency": "L1D Avg AXI Write",
    "l1d_mem_inst_latency": "L1D Avg Mem-Inst",
    "l1i_miss_penalty": "L1I Avg Miss Penalty",
    "l1i_axi_read_latency": "L1I Avg AXI Read",
}

CONFIG_SCAN_FILE = os.path.join(REPO_ROOT, "include", "config.h")

CONFIG_SCAN_SYMBOLS = (
    "FETCH_WIDTH",
    "DECODE_WIDTH",
    "ROB_NUM",
    "ICACHE_LINE_SIZE",
    "ICACHE_OFFSET_BITS",
    "ICACHE_INDEX_BITS",
    "ICACHE_SET_NUM",
    "ICACHE_WAY_NUM",
    "ICACHE_MISS_LATENCY",
    "DCACHE_LINE_SIZE",
    "DCACHE_OFFSET_BITS",
    "DCACHE_INDEX_BITS",
    "DCACHE_SET_NUM",
    "DCACHE_WAY_NUM",
    "CONFIG_AXI_LLC_ENABLE",
    "CONFIG_AXI_LLC_SIZE_BYTES",
    "CONFIG_AXI_LLC_WAYS",
    "CONFIG_AXI_LLC_MSHR_NUM",
    "AXI_KIT_DDR_LATENCY",
)


def _strip_ansi(s):
    return REGEX_ANSI.sub("", s)


def _extract_last_int(text, label):
    m = re.findall(rf"{re.escape(label)}\s*:\s*(\d+)", text)
    return int(m[-1]) if m else None


def _extract_last_pct(text, label):
    m = re.findall(rf"{re.escape(label)}\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*%", text)
    return float(m[-1]) if m else None


def _extract_last_triplet(text, label):
    m = re.findall(rf"{re.escape(label)}\s*:\s*(\d+)\s*/\s*(\d+)\s*/\s*(\d+)", text)
    if not m:
        return None
    return tuple(int(x) for x in m[-1])


def _safe_div(num, den):
    return (num / den) if den > 0 else 0.0


def _safe_pct(num, den):
    return _safe_div(num, den) * 100.0


def _extract_last_section_lines(clean_lines, header_candidates):
    start_idx = -1
    for i in range(len(clean_lines) - 1, -1, -1):
        line = clean_lines[i]
        if any(h in line for h in header_candidates):
            start_idx = i
            break
    if start_idx == -1:
        return []

    section_lines = []
    for line in clean_lines[start_idx + 1 :]:
        if "*********" in line:
            break
        section_lines.append(line.strip())
    return section_lines


def _extract_avg_samples(line):
    m = re.search(r":\s*([-+]?\d+(?:\.\d+)?)\s*cycles\s*\(samples\s*=\s*(\d+)\)", line)
    if not m:
        return None, None
    return float(m.group(1)), int(m.group(2))


def _fmt_bytes(n):
    if n is None:
        return "N/A"
    if n >= (1 << 20):
        return f"{n / (1 << 20):.2f} MiB"
    if n >= (1 << 10):
        return f"{n / (1 << 10):.2f} KiB"
    return f"{n} B"


def _load_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return f.read()
    except Exception:
        return ""


def _strip_inline_comment(s):
    return re.sub(r"//.*$", "", s).strip()


def _find_symbol_raw(text, symbol):
    patterns = [
        rf"^\s*(?:static\s+)?(?:inline\s+)?constexpr\s+[^;=]+\b{re.escape(symbol)}\s*=\s*([^;]+);",
        rf"^\s*(?:static\s+)?const\s+[^;=]+\b{re.escape(symbol)}\s*=\s*([^;]+);",
        rf"^\s*#define\s+{re.escape(symbol)}\s+(.+)$",
    ]
    for pat in patterns:
        m = re.findall(pat, text, flags=re.MULTILINE)
        if m:
            raw = _strip_inline_comment(m[-1])
            if raw:
                return raw
    return None


def _sanitize_expr(expr):
    expr = expr.strip()
    expr = re.sub(r"\btrue\b", "1", expr, flags=re.IGNORECASE)
    expr = re.sub(r"\bfalse\b", "0", expr, flags=re.IGNORECASE)
    expr = re.sub(r"(\d+)[uUlL]+\b", r"\1", expr)
    return expr


def _replace_clog2_calls(expr, known_numeric):
    pat = re.compile(r"clog2\s*\(([^()]+)\)")
    while True:
        m = pat.search(expr)
        if not m:
            break
        inner = m.group(1).strip()
        for name, value in known_numeric.items():
            inner = re.sub(rf"\b{re.escape(name)}\b", str(value), inner)
        if re.search(r"[A-Za-z_]\w*", inner):
            break
        try:
            val = _safe_eval_expr(_sanitize_expr(inner))
            ival = int(round(val))
            if ival <= 0 or (ival & (ival - 1)) != 0:
                break
            rep = str(ival.bit_length() - 1)
            expr = expr[: m.start()] + rep + expr[m.end() :]
        except Exception:
            break
    return expr


def _safe_eval_expr(expr):
    allowed_binops = (
        ast.Add,
        ast.Sub,
        ast.Mult,
        ast.Div,
        ast.FloorDiv,
        ast.Mod,
        ast.LShift,
        ast.RShift,
        ast.BitOr,
        ast.BitAnd,
        ast.BitXor,
    )
    allowed_unary = (ast.UAdd, ast.USub)

    def _eval(node):
        if isinstance(node, ast.Expression):
            return _eval(node.body)
        if isinstance(node, ast.Constant):
            if isinstance(node.value, (int, float)):
                return node.value
            raise ValueError("unsupported constant")
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, allowed_unary):
            v = _eval(node.operand)
            return +v if isinstance(node.op, ast.UAdd) else -v
        if isinstance(node, ast.BinOp) and isinstance(node.op, allowed_binops):
            l = _eval(node.left)
            r = _eval(node.right)
            if isinstance(node.op, ast.Add):
                return l + r
            if isinstance(node.op, ast.Sub):
                return l - r
            if isinstance(node.op, ast.Mult):
                return l * r
            if isinstance(node.op, ast.Div):
                return l / r
            if isinstance(node.op, ast.FloorDiv):
                return l // r
            if isinstance(node.op, ast.Mod):
                return l % r
            if isinstance(node.op, ast.LShift):
                return int(l) << int(r)
            if isinstance(node.op, ast.RShift):
                return int(l) >> int(r)
            if isinstance(node.op, ast.BitOr):
                return int(l) | int(r)
            if isinstance(node.op, ast.BitAnd):
                return int(l) & int(r)
            if isinstance(node.op, ast.BitXor):
                return int(l) ^ int(r)
        raise ValueError("unsupported expression")

    tree = ast.parse(expr, mode="eval")
    return _eval(tree)


def _try_eval_symbol_expr(raw_expr, known_numeric):
    expr = _sanitize_expr(raw_expr)
    for name, value in known_numeric.items():
        expr = re.sub(rf"\b{re.escape(name)}\b", str(value), expr)
    expr = _replace_clog2_calls(expr, known_numeric)
    for name, value in known_numeric.items():
        expr = re.sub(rf"\b{re.escape(name)}\b", str(value), expr)
    if re.search(r"[A-Za-z_]\w*", expr):
        return None
    try:
        v = _safe_eval_expr(expr)
        if isinstance(v, float) and abs(v - round(v)) < 1e-9:
            return int(round(v))
        return v
    except Exception:
        return None


def scan_config_snapshot():
    dbg(f"[CONFIG] scan file: {CONFIG_SCAN_FILE}")
    dbg(f"[CONFIG] exists: {os.path.exists(CONFIG_SCAN_FILE)}")
    text = _load_text(CONFIG_SCAN_FILE)
    if not text:
        return {
            "FETCH_WIDTH": "N/A",
            "DECODE_WIDTH": "N/A",
            "ROB_NUM": "N/A",
            "CACHE_HIERARCHY": "N/A",
            "L1I_GEOM": "N/A",
            "L1I_SIZE": "N/A",
            "L1D_GEOM": "N/A",
            "L1D_SIZE": "N/A",
            "LLC_ENABLE": "N/A",
            "LLC_SIZE": "N/A",
            "LLC_WAYS": "N/A",
            "LLC_MSHR": "N/A",
            "L1I_MISS_LATENCY": "N/A",
            "DDR_LATENCY": "N/A",
        }

    raw_values = {}
    for sym in CONFIG_SCAN_SYMBOLS:
        raw = _find_symbol_raw(text, sym)
        if raw is not None:
            raw_values[sym] = raw

    numeric_values = {}
    for _ in range(8):
        progressed = False
        for sym, raw in raw_values.items():
            if sym in numeric_values:
                continue
            v = _try_eval_symbol_expr(raw, numeric_values)
            if v is not None:
                numeric_values[sym] = v
                progressed = True
        if not progressed:
            break

    def n(sym):
        return numeric_values.get(sym, None)

    fetch_width = n("FETCH_WIDTH")
    decode_width = n("DECODE_WIDTH")
    rob_num = n("ROB_NUM")
    l1i_line = n("ICACHE_LINE_SIZE")
    l1i_sets = n("ICACHE_SET_NUM")
    l1i_ways = n("ICACHE_WAY_NUM")
    l1d_line = n("DCACHE_LINE_SIZE")
    l1d_sets = n("DCACHE_SET_NUM")
    l1d_ways = n("DCACHE_WAY_NUM")
    llc_en = n("CONFIG_AXI_LLC_ENABLE")
    llc_size = n("CONFIG_AXI_LLC_SIZE_BYTES")
    llc_ways = n("CONFIG_AXI_LLC_WAYS")
    llc_mshr = n("CONFIG_AXI_LLC_MSHR_NUM")
    ddr_lat = n("AXI_KIT_DDR_LATENCY")
    l1i_miss_lat = n("ICACHE_MISS_LATENCY")

    l1i_size = None
    if None not in (l1i_line, l1i_sets, l1i_ways):
        l1i_size = int(l1i_line * l1i_sets * l1i_ways)
    l1d_size = None
    if None not in (l1d_line, l1d_sets, l1d_ways):
        l1d_size = int(l1d_line * l1d_sets * l1d_ways)

    llc_enabled = None
    hierarchy = "N/A"
    if llc_en is not None:
        llc_enabled = int(llc_en) != 0
        hierarchy = "L1I + L1D + LLC + DDR" if llc_enabled else "L1I + L1D + DDR"

    return {
        "FETCH_WIDTH": str(fetch_width) if fetch_width is not None else "N/A",
        "DECODE_WIDTH": str(decode_width) if decode_width is not None else "N/A",
        "ROB_NUM": str(rob_num) if rob_num is not None else "N/A",
        "CACHE_HIERARCHY": hierarchy,
        "L1I_GEOM": (
            f"{l1i_ways}w x {l1i_sets}s x {l1i_line}B"
            if None not in (l1i_ways, l1i_sets, l1i_line)
            else "N/A"
        ),
        "L1I_SIZE": _fmt_bytes(l1i_size),
        "L1D_GEOM": (
            f"{l1d_ways}w x {l1d_sets}s x {l1d_line}B"
            if None not in (l1d_ways, l1d_sets, l1d_line)
            else "N/A"
        ),
        "L1D_SIZE": _fmt_bytes(l1d_size),
        "LLC_ENABLE": "ON"
        if llc_enabled
        else ("OFF" if llc_enabled is False else "N/A"),
        "LLC_SIZE": _fmt_bytes(llc_size) if llc_enabled is True else "N/A",
        "LLC_WAYS": str(llc_ways)
        if llc_enabled is True and llc_ways is not None
        else "N/A",
        "LLC_MSHR": str(llc_mshr)
        if llc_enabled is True and llc_mshr is not None
        else "N/A",
        "L1I_MISS_LATENCY": f"{l1i_miss_lat} cyc"
        if l1i_miss_lat is not None
        else "N/A",
        "DDR_LATENCY": f"{ddr_lat} cyc" if ddr_lat is not None else "N/A",
    }


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


def parse_cache_counters(content):
    cache = {
        "l1i_access": None,
        "l1i_hit": None,
        "l1i_miss": None,
        "l1d_req_initial": None,
        "l1d_miss_mshr_alloc": None,
        "llc_read_access": None,
        "llc_read_hit": None,
        "llc_read_miss": None,
        "llc_icache_access": None,
        "llc_icache_hit": None,
        "llc_icache_miss": None,
        "llc_dcache_access": None,
        "llc_dcache_hit": None,
        "llc_dcache_miss": None,
    }

    for key, label in CACHE_COUNTER_LABELS.items():
        cache[key] = _extract_last_int(content, label)

    llc_icache_triplet = _extract_last_triplet(content, CACHE_TRIPLET_LABELS["llc_l1i"])
    if llc_icache_triplet is None:
        llc_icache_triplet = _extract_last_triplet(content, "llc icache a/h/m")
    if llc_icache_triplet is not None:
        (
            cache["llc_icache_access"],
            cache["llc_icache_hit"],
            cache["llc_icache_miss"],
        ) = llc_icache_triplet

    llc_dcache_triplet = _extract_last_triplet(content, CACHE_TRIPLET_LABELS["llc_l1d"])
    if llc_dcache_triplet is None:
        llc_dcache_triplet = _extract_last_triplet(content, "llc dcache a/h/m")
    if llc_dcache_triplet is not None:
        (
            cache["llc_dcache_access"],
            cache["llc_dcache_hit"],
            cache["llc_dcache_miss"],
        ) = llc_dcache_triplet

    # Backward compatibility with older perf_print naming.
    if cache["l1i_hit"] is None:
        cache["l1i_hit"] = _extract_last_int(content, "icache hit")
    if cache["l1i_access"] is None:
        cache["l1i_access"] = _extract_last_int(content, "icache access")
    if (
        cache["l1i_miss"] is None
        and cache["l1i_access"] is not None
        and cache["l1i_hit"] is not None
    ):
        cache["l1i_miss"] = max(cache["l1i_access"] - cache["l1i_hit"], 0)

    # Older logs may only expose dcache access/miss counters; map them to L1D.
    if cache["l1d_req_initial"] is None:
        cache["l1d_req_initial"] = _extract_last_int(content, "dcache access")
    if cache["l1d_miss_mshr_alloc"] is None:
        dcache_miss = _extract_last_int(content, "dcache miss")
        if dcache_miss is not None:
            cache["l1d_miss_mshr_alloc"] = dcache_miss
        else:
            dcache_hit = _extract_last_int(content, "dcache hit")
            dcache_access = cache["l1d_req_initial"]
            if dcache_hit is not None and dcache_access is not None:
                cache["l1d_miss_mshr_alloc"] = max(dcache_access - dcache_hit, 0)

    # Very old generic "cache access/hit" fallback.
    if cache["l1d_req_initial"] is None:
        c_acc_matches = REGEX_CACHE_ACC.findall(content)
        if c_acc_matches:
            cache["l1d_req_initial"] = int(c_acc_matches[-1])
    if cache["l1d_miss_mshr_alloc"] is None:
        c_hit_matches = REGEX_CACHE_HIT.findall(content)
        if c_hit_matches and cache["l1d_req_initial"] is not None:
            cache["l1d_miss_mshr_alloc"] = max(
                cache["l1d_req_initial"] - int(c_hit_matches[-1]), 0
            )

    if (
        cache["llc_read_miss"] is None
        and cache["llc_read_access"] is not None
        and cache["llc_read_hit"] is not None
    ):
        cache["llc_read_miss"] = max(
            cache["llc_read_access"] - cache["llc_read_hit"], 0
        )

    for key in cache.keys():
        if cache[key] is None:
            cache[key] = 0

    return cache


def parse_latency_counters(clean_lines):
    latency = {k: {"avg": 0.0, "samples": 0} for k in LATENCY_METRIC_KEYS}

    l1d_lines = _extract_last_section_lines(
        clean_lines,
        ("*********L1D COUNTER", "*********DCACHE COUNTER"),
    )
    l1i_lines = _extract_last_section_lines(
        clean_lines,
        ("*********L1I COUNTER", "*********ICACHE COUNTER"),
    )

    l1d_prefix_map = {
        "l1d_miss_penalty": "Avg Miss Penalty",
        "l1d_axi_read_latency": "Avg AXI Read Latency",
        "l1d_axi_write_latency": "Avg AXI Write Latency",
        "l1d_mem_inst_latency": "Avg Mem-Inst Latency",
    }
    l1i_prefix_map = {
        "l1i_miss_penalty": "Avg Miss Penalty",
        "l1i_axi_read_latency": "Avg AXI Read",
    }

    for key, prefix in l1d_prefix_map.items():
        for line in l1d_lines:
            if line.startswith(prefix):
                avg, samples = _extract_avg_samples(line)
                if avg is not None and samples is not None:
                    latency[key]["avg"] = avg
                    latency[key]["samples"] = samples
                break

    for key, prefix in l1i_prefix_map.items():
        for line in l1i_lines:
            if line.startswith(prefix):
                avg, samples = _extract_avg_samples(line)
                if avg is not None and samples is not None:
                    latency[key]["avg"] = avg
                    latency[key]["samples"] = samples
                break

    return latency


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
        with open(weight_file, "r") as f:
            for line_idx, line in enumerate(f):
                parts = line.strip().split()
                if not parts:
                    continue
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
        with open(filepath, "r") as f:
            lines = [line.strip() for line in f.readlines()]

        content = _strip_ansi("\n".join(lines))
        clean_lines = content.splitlines()

        # 1. Inst/Cycle
        inst_matches = REGEX_INST.findall(content)
        cyc_matches = REGEX_CYC.findall(content)
        if not inst_matches or not cyc_matches:
            if with_reason:
                return None, "missing instruction/cycle counters"
            return None

        inst = int(inst_matches[-1])
        cyc = int(cyc_matches[-1])
        if inst == 0:
            if with_reason:
                return None, "instruction num is 0"
            return None

        cache = parse_cache_counters(content)
        latency = parse_latency_counters(clean_lines)

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
            "inst": inst,
            "cyc": cyc,
            "cpi": cyc / inst,
            "cache": cache,
            "latency": latency,
            "cache_hit": max(
                cache["l1d_req_initial"] - cache["l1d_miss_mshr_alloc"], 0
            ),  # backward compatibility
            "cache_acc": cache["l1d_req_initial"],  # backward compatibility
            "br_num": br_num_total,
            "br_miss": br_miss_total,
            "tma": tma,
        }
        if with_reason:
            return data, None
        return data

    except Exception as e:
        if with_reason:
            return None, f"parse exception: {e}"
        print(f"Error parsing {filepath}: {e}")
        return None


def check_log_success(filepath):
    try:
        with open(filepath, "r") as f:
            content = _strip_ansi(f.read())
        return ("Success!!!!" in content), None
    except Exception as e:
        return False, f"open/read failed: {e}"


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

    w_cpi_sum = 0.0
    w_weight_sum = 0.0
    w_inst_sum = 0.0
    w_br_hit = 0.0
    w_br_total = 0.0
    w_br_miss = 0.0
    w_cache = {k: 0.0 for k in WEIGHTED_CACHE_KEYS}
    w_lat_total = {k: 0.0 for k in LATENCY_METRIC_KEYS}
    w_lat_samples = {k: 0.0 for k in LATENCY_METRIC_KEYS}

    w_tma_slots = 0.0
    tma_slot_sums = {k: 0.0 for k in TMA_SLOT_KEYS}

    valid_files = 0
    skipped_bad_log = 0
    bad_reasons = {}
    for log_file in useful_logs:
        filename = os.path.basename(log_file)
        sp_match = REGEX_SP_ID.search(filename)
        if not sp_match:
            skipped_bad_log += 1
            bad_reasons["missing sp id after phase-1 filter"] = (
                bad_reasons.get("missing sp id after phase-1 filter", 0) + 1
            )
            continue
        sp_id = int(sp_match.group(1))

        if sp_id not in weights_map:
            skipped_bad_log += 1
            bad_reasons["sp id not in weights after phase-1 filter"] = (
                bad_reasons.get("sp id not in weights after phase-1 filter", 0) + 1
            )
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

        for key in WEIGHTED_CACHE_KEYS:
            w_cache[key] += data["cache"].get(key, 0) * weight
        for key in LATENCY_METRIC_KEYS:
            avg = data["latency"][key]["avg"]
            samples = data["latency"][key]["samples"]
            if samples > 0:
                w_lat_total[key] += weight * avg * samples
                w_lat_samples[key] += weight * samples

        br_correct = data["br_num"] - data["br_miss"]
        w_br_hit += br_correct * weight
        w_br_total += data["br_num"] * weight
        w_br_miss += data["br_miss"] * weight

        # TMA: 用 Total Slots * 百分比恢复为“槽位数”，再进行 simpoint 加权汇总
        if data["tma"] and data["tma"]["total_slots"] > 0:
            tma_total = data["tma"]["total_slots"] * weight
            w_tma_slots += tma_total
            for k in TMA_SLOT_KEYS:
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

    l1d_hit_rate = _safe_pct(
        w_cache["l1d_req_initial"] - w_cache["l1d_miss_mshr_alloc"],
        w_cache["l1d_req_initial"],
    )
    l1d_mpki = _safe_div(w_cache["l1d_miss_mshr_alloc"], w_inst_sum) * 1000.0
    l1i_hit_rate = _safe_pct(w_cache["l1i_hit"], w_cache["l1i_access"])
    l1i_mpki = _safe_div(w_cache["l1i_miss"], w_inst_sum) * 1000.0
    llc_hit_rate = _safe_pct(w_cache["llc_read_hit"], w_cache["llc_read_access"])
    llc_icache_hit_rate = _safe_pct(
        w_cache["llc_icache_hit"], w_cache["llc_icache_access"]
    )
    llc_dcache_hit_rate = _safe_pct(
        w_cache["llc_dcache_hit"], w_cache["llc_dcache_access"]
    )

    br_acc = _safe_pct(w_br_hit, w_br_total)
    br_mpki = _safe_div(w_br_miss, w_inst_sum) * 1000.0

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
    p(f"Cache Hit Rate:     {l1d_hit_rate:.2f} %")
    p(f"Cache MPKI:         {l1d_mpki:.2f}")
    p("Cache Breakdown:")
    p(f"  L1D Hit Rate:     {l1d_hit_rate:.2f} %")
    p(f"  L1D MPKI:         {l1d_mpki:.2f}")
    p(f"  L1I Hit Rate:     {l1i_hit_rate:.2f} %")
    p(f"  L1I MPKI:         {l1i_mpki:.2f}")
    if w_cache["llc_read_access"] > 0:
        p(f"  LLC Read HitRate: {llc_hit_rate:.2f} %")
    else:
        p("  LLC Read HitRate: N/A (disabled or no accesses)")
    if w_cache["llc_icache_access"] > 0:
        p(f"  LLC L1I HitRate:  {llc_icache_hit_rate:.2f} %")
    else:
        p("  LLC L1I HitRate:  N/A")
    if w_cache["llc_dcache_access"] > 0:
        p(f"  LLC L1D HitRate:  {llc_dcache_hit_rate:.2f} %")
    else:
        p("  LLC L1D HitRate:  N/A")
    p("Memory Latency (Weighted):")
    for key in LATENCY_METRIC_KEYS:
        if w_lat_samples[key] > 0:
            avg = _safe_div(w_lat_total[key], w_lat_samples[key])
            p(f"  {LATENCY_METRIC_LABELS[key]}: {avg:.2f} cycles")
        else:
            p(f"  {LATENCY_METRIC_LABELS[key]}: N/A")

    if w_br_total > 0:
        p(f"Branch Accuracy:    {br_acc:.2f} %")
        p(f"Branch MPKI:        {br_mpki:.2f}")
    else:
        p("Branch Accuracy:    N/A (Parsed 0 branches)")

    if w_tma_slots > 0:
        p("TMA (Weighted):")
        p(
            f"  Frontend Bound:   {tma_slot_sums['frontend_bound'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"    Fetch Latency:  {tma_slot_sums['fetch_latency'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"    Fetch BW:       {tma_slot_sums['fetch_bandwidth'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"  Backend Bound:    {tma_slot_sums['backend_bound'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"    Memory Bound:   {tma_slot_sums['memory_bound'] / w_tma_slots * 100:.2f} %"
        )
        p(f"      L1 Bound:     {tma_slot_sums['l1_bound'] / w_tma_slots * 100:.2f} %")
        p(
            f"      Ext Mem Bound:{tma_slot_sums['ext_memory_bound'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"    Core Bound:     {tma_slot_sums['core_bound'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"  Bad Speculation:  {tma_slot_sums['bad_speculation'] / w_tma_slots * 100:.2f} %"
        )
        p(
            f"    Squash Waste:   {tma_slot_sums['squash_waste'] / w_tma_slots * 100:.2f} %"
        )
        p(f"  Retiring:         {tma_slot_sums['retiring'] / w_tma_slots * 100:.2f} %")
    else:
        p("TMA (Weighted):     N/A (Top-Down section not found)")

    if total_insts > 0:
        pred_cycles = total_insts * final_cpi
        pred_time = pred_cycles / (CPU_FREQ_GHZ * 1e9)
        ref_time = REF_TIMES.get(bench_key, 0)
        if pred_time > 0 and ref_time > 0:
            score = ref_time / pred_time
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
    config_snapshot = scan_config_snapshot()
    perf_report_lines.append("CONFIG_SNAPSHOT_BEGIN")
    for k, v in config_snapshot.items():
        perf_report_lines.append(f"{k}={v}")
    perf_report_lines.append("CONFIG_SNAPSHOT_END")
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
    bench_dirs = sorted(
        [d for d in glob.glob(os.path.join(LOG_ROOT_DIR, "*")) if os.path.isdir(d)]
    )
    status_report_lines.append(f"[INFO] benchmark dirs found: {len(bench_dirs)}")
    if not bench_dirs:
        status_report_lines.append(
            "[WARN] No benchmark directories under LOG_ROOT_DIR."
        )
        with open(LOG_STATUS_REPORT, "w") as f:
            f.write("\n".join(status_report_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return
    scores = []
    for d in bench_dirs:
        s, status_lines, perf_lines = process_benchmark(d)
        status_report_lines.extend(status_lines)
        perf_report_lines.extend(perf_lines)
        if s and s > 0:
            scores.append(s)
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
