#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

CONFIG_SMALL="${REPO_ROOT}/include/config.h.small"
CONFIG_ACTIVE="${REPO_ROOT}/include/config.h"
RUN_ALL="${REPO_ROOT}/script/run_all.sh"

JOBS="${JOBS:-6}"
CORE_STEP="${CORE_STEP:-10}"
RESULT_PREFIX="${RESULT_PREFIX:-./results_restore_456}"

if [[ $# -gt 0 ]]; then
  MULTIPLIERS=("$@")
else
  MULTIPLIERS=(1 2 3 4 5 6 7 8)
fi

TMP_DIR="$(mktemp -d)"
cp "${CONFIG_SMALL}" "${TMP_DIR}/config.h.small"
ACTIVE_CONFIG_EXISTED=0
if [[ -f "${CONFIG_ACTIVE}" ]]; then
  cp "${CONFIG_ACTIVE}" "${TMP_DIR}/config.h"
  ACTIVE_CONFIG_EXISTED=1
fi

restore_configs() {
  local rc=$?
  cp "${TMP_DIR}/config.h.small" "${CONFIG_SMALL}" || true
  if [[ "${ACTIVE_CONFIG_EXISTED}" -eq 1 ]]; then
    cp "${TMP_DIR}/config.h" "${CONFIG_ACTIVE}" || true
  else
    rm -f "${CONFIG_ACTIVE}" || true
  fi
  rm -rf "${TMP_DIR}"
  exit "${rc}"
}
trap restore_configs EXIT INT TERM

set_load_window_width() {
  local multiplier="$1"

  if ! [[ "${multiplier}" =~ ^[0-9]+$ ]]; then
    echo "Error: multiplier must be a positive integer, got: ${multiplier}" >&2
    exit 1
  fi
  if [[ "${multiplier}" -lt 1 ]]; then
    echo "Error: multiplier must be >= 1, got: ${multiplier}" >&2
    exit 1
  fi

  local match_count
  match_count="$(grep -Ec 'constexpr[[:space:]]+int[[:space:]]+LOAD_WINDOWS_WIDTH[[:space:]]*=' "${CONFIG_SMALL}" || true)"
  if [[ "${match_count}" -ne 1 ]]; then
    echo "Error: expected exactly one LOAD_WINDOWS_WIDTH definition in ${CONFIG_SMALL}, found ${match_count}" >&2
    exit 1
  fi

  perl -0pi -e "s/constexpr\\s+int\\s+LOAD_WINDOWS_WIDTH\\s*=\\s*[^;]+;/constexpr int LOAD_WINDOWS_WIDTH = LSU_LDU_COUNT * ${multiplier};/" "${CONFIG_SMALL}"
}

echo "LOAD_WINDOWS_WIDTH sweep"
echo "Repo: ${REPO_ROOT}"
echo "Build jobs: ${JOBS}"
echo "Multipliers: ${MULTIPLIERS[*]}"
echo

for multiplier in "${MULTIPLIERS[@]}"; do
  build_dir="build_${multiplier}"
  simulator="./${build_dir}/simulator"
  result_dir="${RESULT_PREFIX}_${multiplier}"
  core_start=$((CORE_STEP * multiplier))

  echo "=================================================="
  echo "Multiplier:        ${multiplier}"
  echo "LOAD_WINDOWS_WIDTH LSU_LDU_COUNT * ${multiplier}"
  echo "Build dir:         ./${build_dir}"
  echo "Simulator:         ${simulator}"
  echo "Result dir:        ${result_dir}"
  echo "Core start:        ${core_start}"
  echo "=================================================="

  set_load_window_width "${multiplier}"
  make -j"${JOBS}" BUILD_DIR="${build_dir}" small

  if [[ ! -f "${simulator}" ]]; then
    echo "Error: simulator was not built: ${simulator}" >&2
    exit 1
  fi

  SIMULATOR="${simulator}" \
  RESULT_DIR="${result_dir}" \
  CORE_START="${core_start}" \
  MAX_JOBS="${JOBS}" \
    bash "${RUN_ALL}"
done

echo "All LOAD_WINDOWS_WIDTH sweep runs completed."
