#!/bin/bash

# ================= 配置区域 =================
SIMULATOR="${SIMULATOR:-./build/simulator}"
CKPT_ROOT="${CKPT_ROOT:-/share/personal/S/houruyao/simpoint/rv32imab_ckpt_1gb_ram/456.hmmer_ref/}"
RESULT_DIR="${RESULT_DIR:-./results_restore_456}"
CKPT_WARMUP="${CKPT_WARMUP-10000000}"
CKPT_MAX_COMMIT="${CKPT_MAX_COMMIT-10000000}"
CORE_START="${CORE_START:-0}"

MAX_JOBS="${MAX_JOBS:-64}"

# 捕获 Ctrl+C (SIGINT)，优雅退出
trap 'echo -e "\n🛑 接收到退出信号，正在清理所有后台模拟器..."; kill 0; exit 1' SIGINT
# ===========================================

if [ ! -f "$SIMULATOR" ]; then
  echo "Error: Simulator not found at $SIMULATOR"
  exit 1
fi

if [ ! -d "$CKPT_ROOT" ]; then
  echo "Error: Checkpoint root dir not found: $CKPT_ROOT"
  exit 1
fi

START_TIME=$(date +%s)
echo "=================================================="
echo "Start Time:     $(date)"
echo "Mode:           Static Modulo Partitioning (Zero-Race)"
echo "Parallel Jobs:  $MAX_JOBS"
echo "=================================================="

echo "Scanning for all checkpoint files..."
mapfile -t SORTED_CKPTS < <(find "$CKPT_ROOT" -name "*.gz" | sort)
ALL_CKPTS=()
for ckpt in "${SORTED_CKPTS[@]}"; do
  if [[ "$ckpt" == *"/429.mcf_ref/"* ]]; then
    ALL_CKPTS+=("$ckpt")
  fi
done
for ckpt in "${SORTED_CKPTS[@]}"; do
  if [[ "$ckpt" != *"/429.mcf_ref/"* ]]; then
    ALL_CKPTS+=("$ckpt")
  fi
done
TOTAL_TASKS=${#ALL_CKPTS[@]}

if [ "$TOTAL_TASKS" -eq 0 ]; then
  echo "Error: No .gz files found in $CKPT_ROOT"
  exit 1
fi

echo "Found $TOTAL_TASKS checkpoints. Preparing directories..."

for ckpt_file in "${ALL_CKPTS[@]}"; do
  bench_name=$(basename "$(dirname "$ckpt_file")")
  mkdir -p "$RESULT_DIR/$bench_name"
done

# ================= 核心魔法：打印播音员 (防输出乱码) =================
PRINT_FIFO="/tmp/sim_print_$$"
mkfifo "$PRINT_FIFO"
exec 4<>"$PRINT_FIFO"
rm "$PRINT_FIFO"

(
  while read -u 4 msg; do
    if [ "$msg" == "PRINT_EOF" ]; then
      break
    fi
    echo "$msg"
  done
) &
PRINTER_PID=$!

# ================= 核心重构：静态取模分配 =================
echo "Launching $MAX_JOBS dedicated workers pinned to cores ${CORE_START}-$((CORE_START + MAX_JOBS - 1))..."

WORKER_PIDS=()

for ((worker = 0; worker < MAX_JOBS; worker++)); do
  (
    core=$((CORE_START + worker))

    # 核心逻辑：这个 Worker 只认属于自己编号的任务
    for ((i = worker; i < TOTAL_TASKS; i += MAX_JOBS)); do
      ckpt_file="${ALL_CKPTS[$i]}"

      bench_name=$(basename "$(dirname "$ckpt_file")")
      ckpt_basename=$(basename "$ckpt_file" .gz)
      log_file="$RESULT_DIR/$bench_name/${ckpt_basename}.log"

      sim_args=("$SIMULATOR" --mode ckpt)
      if [ -n "$CKPT_WARMUP" ]; then
        sim_args+=(-w "$CKPT_WARMUP")
      fi
      if [ -n "$CKPT_MAX_COMMIT" ]; then
        sim_args+=(-c "$CKPT_MAX_COMMIT")
      fi

      # 强行绑定物理核开跑
      taskset -c "$core" "${sim_args[@]}" "$ckpt_file" >"$log_file" 2>&1

      if [ $? -eq 0 ]; then
        echo "[Done] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename" >&4
      else
        echo "[Fail] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename" >&4
      fi
    done
  ) &
  WORKER_PIDS+=($!)
done

# ================= 等待与收尾 =================
wait "${WORKER_PIDS[@]}"

echo "PRINT_EOF" >&4
wait $PRINTER_PID

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

hours=$((DURATION / 3600))
minutes=$(((DURATION % 3600) / 60))
seconds=$((DURATION % 60))

echo "=================================================="
echo "End Time:        $(date)"
echo "Total Duration:  ${hours}h ${minutes}m ${seconds}s"
echo "All tasks processed safely."
echo "=================================================="
