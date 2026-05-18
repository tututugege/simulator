#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

SIMULATOR="${SIMULATOR:-./build/simulator}"
CKPT_ROOT="${CKPT_ROOT:-${REPO_ROOT}/..}"
RESULT_DIR="${RESULT_DIR:-./results_parent_ckpts_8phys}"
MAX_JOBS="${MAX_JOBS:-8}"
CKPT_WARMUP="${CKPT_WARMUP-}"
CKPT_MAX_COMMIT="${CKPT_MAX_COMMIT-}"
PIN_CPUS="${PIN_CPUS:-}"
DRY_RUN="${DRY_RUN:-0}"

usage() {
  cat <<'EOF'
Usage:
  script/run_parent_ckpts_8phys.sh

Environment overrides:
  SIMULATOR       Simulator binary. Default: ./build/simulator
  CKPT_ROOT       Directory containing ckpt files. Default: parent directory
  RESULT_DIR      Directory for per-ckpt logs. Default: ./results_parent_ckpts_8phys
  MAX_JOBS        Parallel pinned workers. Default: 8
  PIN_CPUS        Comma/space separated logical CPU IDs to use.
                  If unset, the script selects one online logical CPU from each
                  physical core via lscpu, avoiding hyperthread siblings.
  CKPT_WARMUP     Optional value passed as -w/--warmup.
  CKPT_MAX_COMMIT Optional value passed as -c/--max-commit.
  DRY_RUN=1       Print the launch plan without running the simulator.

Examples:
  script/run_parent_ckpts_8phys.sh
  CKPT_WARMUP=10000000 CKPT_MAX_COMMIT=10000000 script/run_parent_ckpts_8phys.sh
  PIN_CPUS=0,2,4,6,8,10,12,14 script/run_parent_ckpts_8phys.sh
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

if [[ "${1-}" == "-h" || "${1-}" == "--help" ]]; then
  usage
  exit 0
fi

[[ "${MAX_JOBS}" =~ ^[0-9]+$ ]] || die "MAX_JOBS must be a positive integer, got: ${MAX_JOBS}"
(( MAX_JOBS > 0 )) || die "MAX_JOBS must be > 0"

command -v taskset >/dev/null 2>&1 || die "taskset is required"
command -v lscpu >/dev/null 2>&1 || die "lscpu is required"
[[ -x "${SIMULATOR}" ]] || die "simulator not found or not executable: ${SIMULATOR}"
[[ -d "${CKPT_ROOT}" ]] || die "checkpoint directory not found: ${CKPT_ROOT}"

mapfile -t CKPTS < <(
  find "${CKPT_ROOT}" -maxdepth 1 -type f \
    \( -name 'ckpt*.gz' -o -name '*.ckpt' -o -name '*.ckpt.gz' \) \
    | sort
)

TOTAL_TASKS="${#CKPTS[@]}"
(( TOTAL_TASKS > 0 )) || die "no checkpoint files found in ${CKPT_ROOT}"

discover_physical_cpus() {
  lscpu -p=CPU,CORE,SOCKET,ONLINE \
    | awk -F, -v max="${MAX_JOBS}" '
        /^#/ { next }
        $4 != "Y" { next }
        {
          key = $3 ":" $2
          if (!(key in seen)) {
            seen[key] = 1
            cpus[++n] = $1
            if (n == max) {
              exit
            }
          }
        }
        END {
          for (i = 1; i <= n; i++) {
            printf "%s%s", (i == 1 ? "" : " "), cpus[i]
          }
          if (n > 0) {
            printf "\n"
          }
        }
      '
}

if [[ -n "${PIN_CPUS}" ]]; then
  read -r -a SELECTED_CPUS <<<"$(tr ',:' '  ' <<<"${PIN_CPUS}")"
else
  read -r -a SELECTED_CPUS <<<"$(discover_physical_cpus)"
fi

(( ${#SELECTED_CPUS[@]} >= MAX_JOBS )) \
  || die "need ${MAX_JOBS} physical cores, got ${#SELECTED_CPUS[@]} CPUs: ${SELECTED_CPUS[*]:-<none>}"

SELECTED_CPUS=("${SELECTED_CPUS[@]:0:MAX_JOBS}")

mkdir -p "${RESULT_DIR}"

SIM_ARGS=("${SIMULATOR}" --mode ckpt)
if [[ -n "${CKPT_WARMUP}" ]]; then
  SIM_ARGS+=(-w "${CKPT_WARMUP}")
fi
if [[ -n "${CKPT_MAX_COMMIT}" ]]; then
  SIM_ARGS+=(-c "${CKPT_MAX_COMMIT}")
fi

echo "=================================================="
echo "Start Time:    $(date)"
echo "Simulator:     ${SIMULATOR}"
echo "Ckpt root:     ${CKPT_ROOT}"
echo "Result dir:    ${RESULT_DIR}"
echo "Checkpoints:   ${TOTAL_TASKS}"
echo "Workers:       ${MAX_JOBS}"
echo "Pinned CPUs:   ${SELECTED_CPUS[*]}"
echo "Mode:          one logical CPU per physical core"
echo "=================================================="

if [[ "${DRY_RUN}" == "1" ]]; then
  for ((i = 0; i < TOTAL_TASKS; i++)); do
    cpu="${SELECTED_CPUS[$((i % MAX_JOBS))]}"
    ckpt_base="$(basename "${CKPTS[$i]}")"
    echo "[DryRun] CPU ${cpu} <- ${ckpt_base}"
  done
  exit 0
fi

START_TIME="$(date +%s)"
WORKER_PIDS=()

terminate_workers() {
  echo
  echo "Interrupted; stopping simulator workers..."
  for pid in "${WORKER_PIDS[@]:-}"; do
    kill "${pid}" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  exit 130
}
trap terminate_workers INT TERM

run_worker() {
  local worker_id="$1"
  local cpu="$2"
  local failed=0

  for ((task = worker_id; task < TOTAL_TASKS; task += MAX_JOBS)); do
    local ckpt_file="${CKPTS[$task]}"
    local ckpt_base
    local log_file
    ckpt_base="$(basename "${ckpt_file}")"
    log_file="${RESULT_DIR}/${ckpt_base%.gz}.log"

    echo "[Start] worker=${worker_id} cpu=${cpu} ckpt=${ckpt_base}"
    if taskset -c "${cpu}" "${SIM_ARGS[@]}" "${ckpt_file}" >"${log_file}" 2>&1; then
      echo "[Done]  worker=${worker_id} cpu=${cpu} ckpt=${ckpt_base} log=${log_file}"
    else
      echo "[Fail]  worker=${worker_id} cpu=${cpu} ckpt=${ckpt_base} log=${log_file}" >&2
      failed=1
    fi
  done

  return "${failed}"
}

for ((worker = 0; worker < MAX_JOBS; worker++)); do
  run_worker "${worker}" "${SELECTED_CPUS[$worker]}" &
  WORKER_PIDS+=("$!")
done

FAILED=0
set +e
for pid in "${WORKER_PIDS[@]}"; do
  wait "${pid}"
  rc="$?"
  if (( rc != 0 )); then
    FAILED=1
  fi
done
set -e

END_TIME="$(date +%s)"
DURATION="$((END_TIME - START_TIME))"

echo "=================================================="
echo "End Time:      $(date)"
echo "Duration:      $((DURATION / 3600))h $(((DURATION % 3600) / 60))m $((DURATION % 60))s"
if (( FAILED == 0 )); then
  echo "Result:        all checkpoint runs completed"
else
  echo "Result:        one or more checkpoint runs failed"
fi
echo "=================================================="

exit "${FAILED}"
