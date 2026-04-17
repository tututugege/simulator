#!/bin/bash

# ================= 配置区域 =================
SIMULATOR="./build/simulator"
CKPT_ROOT="/share/personal/S/houruyao/simpoint/rv32imab_ckpt_1gb_ram"
RESULT_DIR="./results_restore"
# 留空="" 时不传 -w，simulator 默认使用 checkpoint_interval 作为 warmup。
CKPT_WARMUP="10000000"
# 留空="" 时不传 -c，simulator 默认使用 checkpoint_interval 作为 measure 长度。
CKPT_MAX_COMMIT="10000000"
CORE_START=0

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
echo "CKPT Warmup:    ${CKPT_WARMUP:-<simulator default>}"
echo "CKPT MaxCommit: ${CKPT_MAX_COMMIT:-<simulator default>}"
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

# ================= 核心魔法：无死锁 FIFO 任务队列 =================

# 创建命名管道文件
FIFO_FILE="/tmp/sim_queue_$$"
mkfifo "$FIFO_FILE"

# [终极修复] 使用 <> (读写双向模式) 打开，Linux 内核就不会阻塞我们啦！
exec 3<> "$FIFO_FILE"

# 绑定完毕后立刻隐藏删除（进程退出自动回收）
rm "$FIFO_FILE"

echo "Populating task queue..."
# [生产者] 将所有任务路径一次性塞入队列
for ckpt_file in "${ALL_CKPTS[@]}"; do
    echo "$ckpt_file" >&3
done

# [新增魔法] 塞入 128 颗“毒药药丸(Poison Pill)”
# 这样 128 个 Worker 只要读到这个信号，就知道任务干完了，自然退出
for ((i=0; i<MAX_JOBS; i++)); do
    echo "EOF_SIGNAL" >&3
done

echo "Launching $MAX_JOBS dedicated workers pinned to cores 0-$((MAX_JOBS-1))..."

# [消费者] 启动 128 个长期存活的 Worker 进程
for ((worker=0; worker<MAX_JOBS; worker++)); do
    (
        core=$((CORE_START + worker))
        # 大家一起从 3 号文件描述符抢任务
        while read -u 3 ckpt_file; do

            # 吃到毒药，打卡下班！
            if [ "$ckpt_file" == "EOF_SIGNAL" ]; then
                break
            fi

            bench_name=$(basename "$(dirname "$ckpt_file")")
            ckpt_basename=$(basename "$ckpt_file" .gz)
            log_file="$RESULT_DIR/$bench_name/${ckpt_basename}.log"

            # 按配置选择性透传 -w / -c；留空时沿用 simulator 默认行为。
            sim_args=("$SIMULATOR" --mode ckpt)
            if [ -n "$CKPT_WARMUP" ]; then
                sim_args+=(-w "$CKPT_WARMUP")
            fi
            if [ -n "$CKPT_MAX_COMMIT" ]; then
                sim_args+=(-c "$CKPT_MAX_COMMIT")
            fi

            # 强行绑定物理核，开跑！
            taskset -c "$core" "${sim_args[@]}" "$ckpt_file" > "$log_file" 2>&1

            if [ $? -eq 0 ]; then
                echo "[Done] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename"
            else
                echo "[Fail] Core $(printf "%03d" $core) | $bench_name/$ckpt_basename"
            fi
        done
    ) &
done

# 等待所有 128 个后台 Worker 执行完毕
wait
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
