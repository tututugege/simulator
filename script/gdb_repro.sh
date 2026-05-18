#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

MODE="run"
TARGET="./baremetal/memory"
FAST_FORWARD=""
WARMUP=""
MAX_COMMIT=""
PROFILE="${PROFILE:-default}"
BUILD_BEFORE_RUN=1
GDB_CMDS="$ROOT_DIR/script/gdb/sim.gdb"

usage() {
  cat <<'EOF'
Usage:
  script/gdb_repro.sh [options] [target]

Options:
  --mode <run|ckpt|fast|ref>   Simulator mode (default: run)
  -f, --fast-forward <num>     Required for fast mode
  -w, --warmup <num>           Warmup steps for ckpt mode
  -c, --max-commit <num>       Commit limit to shrink the repro window
  --profile <name>             Build profile passed to make (default: default)
  --no-build                   Reuse current build/simulator
  --gdb-cmds <path>            Override default gdb command file
  -h, --help                   Show this help

Examples:
  script/gdb_repro.sh
  script/gdb_repro.sh --mode fast -f 1000000 ./baremetal/memory
  script/gdb_repro.sh --mode ckpt -w 500000 -c 2000000 /path/to/ckpt.gz
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --mode)
    MODE="$2"
    shift 2
    ;;
  -f|--fast-forward)
    FAST_FORWARD="$2"
    shift 2
    ;;
  -w|--warmup)
    WARMUP="$2"
    shift 2
    ;;
  -c|--max-commit)
    MAX_COMMIT="$2"
    shift 2
    ;;
  --profile)
    PROFILE="$2"
    shift 2
    ;;
  --gdb-cmds)
    GDB_CMDS="$2"
    shift 2
    ;;
  --no-build)
    BUILD_BEFORE_RUN=0
    shift
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  --)
    shift
    break
    ;;
  -*)
    echo "Unknown option: $1" >&2
    usage >&2
    exit 1
    ;;
  *)
    TARGET="$1"
    shift
    ;;
  esac
done

if [[ $# -gt 0 ]]; then
  TARGET="$1"
fi

if [[ "$MODE" == "fast" && -z "$FAST_FORWARD" ]]; then
  echo "fast mode requires --fast-forward/-f" >&2
  exit 1
fi

if [[ $BUILD_BEFORE_RUN -eq 1 ]]; then
  make DEBUG=1 PROFILE="$PROFILE"
fi

SIM_ARGS=(./build/simulator --mode "$MODE")
if [[ -n "$FAST_FORWARD" ]]; then
  SIM_ARGS+=(-f "$FAST_FORWARD")
fi
if [[ -n "$WARMUP" ]]; then
  SIM_ARGS+=(-w "$WARMUP")
fi
if [[ -n "$MAX_COMMIT" ]]; then
  SIM_ARGS+=(-c "$MAX_COMMIT")
fi
SIM_ARGS+=("$TARGET")

exec gdb -x "$GDB_CMDS" --args "${SIM_ARGS[@]}"
