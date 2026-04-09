#!/bin/bash

# ================= 配置区域 =================
SIMULATOR="./build/simulator"
CKPT_ROOT="/share/personal/S/houruyao/simpoint/rv32imab_ckpt_10M"
RESULT_DIR="./results_restore"

# 内存够的话建议等于可用的核心数 不用超线程
# 一个进程需要8GB 开完美分支预测需要12GB
MAX_JOBS=64
# ===========================================

# 基础检查
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
echo "Mode:           Strict Physical Core Binding (FIFO Queue)"
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

# [优化 1] 预先创建所有子目录，绝对避免多进程并发 mkdir 导致的竞争报错
for ckpt_file in "${ALL_CKPTS[@]}"; do
    bench_name=$(basename "$(dirname "$ckpt_file")")
    mkdir -p "$RESULT_DIR/$bench_name"
done


# ================= 奈奈子终极改良版核心魔法：真·无死锁 FIFO =================

# 1. 建立队列管道
FIFO_FILE="/tmp/sim_queue_$$"
mkfifo "$FIFO_FILE"
exec 3<> "$FIFO_FILE"
rm "$FIFO_FILE"

# 2. 建立锁文件（注意：这里先不要 rm，保留物理文件让 Worker 去打开）
LOCK_FILE="/tmp/sim_lock_$$"
touch "$LOCK_FILE"

echo "Populating task queue..."
# [生产者] 放入后台独立运行
(
    for ckpt_file in "${ALL_CKPTS[@]}"; do
        echo "$ckpt_file" >&3
    done

    # 塞入 64 颗“毒药药丸”
    for ((i=0; i<MAX_JOBS; i++)); do
        echo "EOF_SIGNAL" >&3
    done
) & 

echo "Launching $MAX_JOBS dedicated workers pinned to cores 0-$((MAX_JOBS-1))..."

# [消费者] 启动 64 个 Worker
for ((core=0; core<MAX_JOBS; core++)); do
    (
        # 【终极修复】：在 Worker 进程内部独立打开锁文件！获取专属的 file description
        exec 4< "$LOCK_FILE"

        while true; do
            # 现在锁终于可以生效了！
            flock -x 4
            read -r -u 3 ckpt_file
            flock -u 4

            if [ "$ckpt_file" == "EOF_SIGNAL" ]; then
                break
            fi

            if [ -z "$ckpt_file" ]; then
                continue
            fi

            bench_name=$(basename "$(dirname "$ckpt_file")")
            ckpt_basename=$(basename "$ckpt_file" .gz)
            log_file="$RESULT_DIR/$bench_name/${ckpt_basename}.log"

            # 强行绑定物理核开跑
            taskset -c "$core" $SIMULATOR --mode ckpt -w 10000000 "$ckpt_file" > "$log_file" 2>&1

            if [ $? -eq 0 ]; then
                echo "[Done] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename"
            else
                echo "[Fail] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename"
            fi
        done
    ) &
done

# 等待所有后台 Worker 执行完毕
wait

# 大家干完活了，再清理掉锁文件
rm -f "$LOCK_FILE"
# ================================================================

# ================= 统计 =================
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

hours=$((DURATION / 3600))
minutes=$(((DURATION % 3600) / 60))
seconds=$((DURATION % 60))

echo "=================================================="
echo "End Time:       $(date)"
echo "Total Duration: ${hours}h ${minutes}m ${seconds}s"
echo "All tasks processed safely."
echo "=================================================="
