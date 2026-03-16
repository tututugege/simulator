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
    
    # ----------------------------------------------------
    # 1. 第一次执行 (R_S 配置: LSU=0, DCACHE=0)
    # ----------------------------------------------------
    LOG_RS="R_S_${SIM_TIME}.txt"
    echo "[1/2] 正在修改配置文件为 R_S 模式..."
    
    # 使用 sed 正则替换整行内容 (针对 #define 格式)
    # 如果你的文件是 KEY=VALUE 格式，请将后面的替换字符串改为 CONFIG_...=0
    sed -i "s/^.*CONFIG_BACKEND_USE_SIMPLE_LSU.*$/#define CONFIG_BACKEND_USE_SIMPLE_LSU 0/" "$CONFIG_FILE"
    sed -i "s/^.*CONFIG_PERF_SNAPSHOT_SIM_TIME.*$/#define CONFIG_PERF_SNAPSHOT_SIM_TIME $SIM_TIME/" "$CONFIG_FILE"

    echo "[1/2] 清理并编译运行，日志保存至: $LOG_RS"
    
    # 必须执行 make clean，否则修改了宏定义文件也不会触发重新编译
    make clean
    make -j8 > "$LOG_RS" 2>&1
        
    if [ $? -ne 0 ]; then
        echo "错误: R_S 配置执行失败，请检查日志 $LOG_RS"
        exit 1
    fi

    # ----------------------------------------------------
    # 2. 第二次执行 (S_S 配置: LSU=1, DCACHE=1)
    # ----------------------------------------------------
    LOG_SS="S_S_${SIM_TIME}.txt"
    echo "[2/2] 正在修改配置文件为 S_S 模式..."
    
    sed -i "s/^.*CONFIG_BACKEND_USE_SIMPLE_LSU.*$/#define CONFIG_BACKEND_USE_SIMPLE_LSU 1/" "$CONFIG_FILE"
    sed -i "s/^.*CONFIG_PERF_SNAPSHOT_SIM_TIME.*$/#define CONFIG_PERF_SNAPSHOT_SIM_TIME $SIM_TIME/" "$CONFIG_FILE"

    echo "[2/2] 清理并编译运行，日志保存至: $LOG_SS"
    
    make clean
    make -j8 > "$LOG_SS" 2>&1
        
    if [ $? -ne 0 ]; then
        echo "错误: S_S 配置执行失败，请检查日志 $LOG_SS"
        exit 1
    fi

    echo "第 $i 次循环完成！"
    echo "=================================================="
    
    i=$(( i + 1 ))
done

echo "所有 $N 次测试均已成功执行完毕！"