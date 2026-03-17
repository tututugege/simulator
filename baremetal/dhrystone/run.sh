#!/bin/bash

# 检查是否传入了参数 n
if [ -z "$1" ]; then
    echo "用法: $0 <运行次数 n>"
    echo "例如: $0 5"
    exit 1
fi

N=$1

# ==========================================
# 用户配置区：请填写包含这些宏定义的实际文件路径
# 例如："include/config.h" 或 ".config" 等
# ==========================================
CONFIG_FILE="../../include/config.h"

# 检查配置文件是否存在
if [ ! -f "$CONFIG_FILE" ]; then
    echo "错误: 找不到配置文件 '$CONFIG_FILE'，请在脚本中修改正确的文件路径。"
    exit 1
fi

# 使用兼容 sh 的 while 循环，避免语法报错
i=1
while [ "$i" -le "$N" ]
do
    SIM_TIME=$(( i * 10000 ))
    
    echo "=================================================="
    echo "开始第 $i 次循环，配置 SIM_TIME = $SIM_TIME"

    LOG_RUN="R_${SIM_TIME}.txt"
    sed -i "s/^.*CONFIG_PERF_SNAPSHOT_SIM_TIME.*$/#define CONFIG_PERF_SNAPSHOT_SIM_TIME $SIM_TIME/" "$CONFIG_FILE"

    echo "[1/1] 清理并编译运行，日志保存至: $LOG_RUN"
    
    # 必须执行 make clean，否则修改了宏定义文件也不会触发重新编译
    make clean
    make -j8 > "$LOG_RUN" 2>&1
        
    if [ $? -ne 0 ]; then
        echo "错误: 执行失败，请检查日志 $LOG_RUN"
        exit 1
    fi

    echo "第 $i 次循环完成！"
    echo "=================================================="
    
    i=$(( i + 1 ))
done

echo "所有 $N 次测试均已成功执行完毕！"
