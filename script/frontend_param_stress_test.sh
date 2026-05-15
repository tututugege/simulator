#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

JOBS="${JOBS:-$(nproc)}"
LOG_DIR="${REPO_ROOT}/build/frontend_param_stress_logs"
mkdir -p "${LOG_DIR}"

TARGETS=(
  "build/front-end/front_top.o"
  "build/front-end/predecode.o"
  "build/front-end/predecode_checker.o"
  "build/front-end/fifo/fetch_address_FIFO.o"
  "build/front-end/fifo/instruction_FIFO.o"
  "build/front-end/fifo/PTAB.o"
  "build/front-end/fifo/front2bank_FIFO.o"
)

PASS_CASES=(
  "baseline-default|"
  "bank17-sram-delay|-DBPU_BANK_NUM=17 -DSRAM_DELAY_ENABLE -DSRAM_DELAY_MIN=0 -DSRAM_DELAY_MAX=2"
  "type-predictor-sweep|-DBPU_BANK_NUM=24 -DTYPE_PRED_ENTRY_NUM=2048 -DTYPE_PRED_WAY_NUM=4 -DTYPE_PRED_TAG_WIDTH=10 -DTYPE_PRED_CONF_BITS=3 -DTYPE_PRED_AGE_BITS=2"
  "tage-scl-sweep|-DBPU_BANK_NUM=24 -DTAGE_SC_ENTRY_NUM=2048 -DTAGE_SC_L_ENTRY_NUM=2048 -DTAGE_SC_PATH_BITS=24 -DTAGE_LOOP_ENTRY_NUM=2048 -DTAGE_LOOP_TAG_BITS=10 -DTAGE_LOOP_CONF_BITS=3"
  "btb-tc-sweep|-DBPU_BANK_NUM=20 -DBTB_WAY_NUM=2 -DBTB_TAG_LEN=10 -DBHT_ENTRY_NUM=4096 -DTC_ENTRY_NUM=1024 -DTC_WAY_NUM=4 -DTC_TAG_LEN=12 -DINDIRECT_BTB_INIT_USEFUL=3 -DINDIRECT_TC_INIT_USEFUL=5"
  "fifo-queue-sweep|-DBPU_BANK_NUM=18 -DQ_DEPTH=1024 -DINSTRUCTION_FIFO_SIZE=16 -DPTAB_SIZE=16 -DFETCH_ADDR_FIFO_SIZE=16 -DFRONT2BACK_FIFO_SIZE=32"
  "diag-switch-sweep|-DBPU_BANK_NUM=17 -DDEBUG_PRINT=1 -DDEBUG_PRINT_SMALL=1 -DFRONTEND_ENABLE_RUNTIME_STATS_SUMMARY=1 -DFRONTEND_ENABLE_TRAINING_AREA_STATS=1"
)

FAIL_CASES=(
  "bank-eq-fetch-width|-DBPU_BANK_NUM=16"
  "bank-lt-commit-width|-DBPU_BANK_NUM=4"
  "force-enable-2ahead|-DENABLE_2AHEAD"
  "invalid-nlp-threshold|-DBPU_BANK_NUM=17 -DNLP_CONF_BITS=2 -DNLP_CONF_THRESHOLD=4"
)

pass_count=0
fail_count=0

run_case() {
  local expected="$1"
  local descriptor="$2"
  local name="${descriptor%%|*}"
  local flags="${descriptor#*|}"
  local log_file="${LOG_DIR}/${expected}-${name}.log"

  echo "==> [${expected}] ${name}"
  echo "    flags: ${flags:-<none>}"

  set +e
  make -B -j"${JOBS}" "${TARGETS[@]}" EXTRA_CXXFLAGS="${flags}" >"${log_file}" 2>&1
  local rc=$?
  set -e

  if [[ "${expected}" == "pass" ]]; then
    if [[ ${rc} -eq 0 ]]; then
      echo "    result: PASS (compiled)"
      ((pass_count+=1))
      return 0
    fi
    echo "    result: FAIL (expected pass, got compile error)"
    tail -n 40 "${log_file}" || true
    return 1
  fi

  if [[ ${rc} -ne 0 ]]; then
    echo "    result: PASS (expected failure observed)"
    ((pass_count+=1))
    return 0
  fi

  echo "    result: FAIL (expected failure, but compiled)"
  return 1
}

echo "Frontend non-icache parameter stress test"
echo "Repo: ${REPO_ROOT}"
echo "Logs: ${LOG_DIR}"
echo

for case_item in "${PASS_CASES[@]}"; do
  if ! run_case "pass" "${case_item}"; then
    ((fail_count+=1))
  fi
done

for case_item in "${FAIL_CASES[@]}"; do
  if ! run_case "fail" "${case_item}"; then
    ((fail_count+=1))
  fi
done

echo
echo "Summary: pass_count=${pass_count}, fail_count=${fail_count}"
if [[ ${fail_count} -ne 0 ]]; then
  echo "Stress test finished with failures."
  exit 1
fi

echo "Stress test finished successfully."
