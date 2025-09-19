#!/bin/bash

# ==============================================================================
# Denum 压缩/解压/验证 自动化脚本
#
# 功能:
# 1. 编译压缩和解压程序。
# 2. 遍历数据集列表。
# 3. 对每个数据集，执行：压缩 -> 解压 -> 验证 -> 清理。
#
# 使用方法:
# 1. 将此脚本放置在与 C++ 源文件相同的目录中。
# 2. 确保目录结构符合脚本预期 (特别是原始 Logs 目录的位置)。
# 3. 给予执行权限: chmod +x run_experiments.sh
# 4. 运行脚本: ./run_experiments.sh
# ==============================================================================

# --- 配置 ---

# 如果命令失败，立即退出脚本
set -e

# 数据集列表 (根据您提供的数据)
DATASETS=(

    "bytedance_C"

)

# 压缩和解压参数
BLOCK_SIZE=100000
COMPRESS_MODE=1
THREADS=4

# 可执行文件名 (请确保这些文件存在于当前目录)
COMPRESSOR_EXE="./compressor"
DECOMPRESSOR_EXE="./decompressor"

# 目录路径
RESULTS_BASE_DIR="Baseline/Denum/results"

# --- 脚本开始 ---

echo "============================================="
echo "      Denum 运行与清理脚本"
echo "============================================="
echo "前提: 必须已存在 ${COMPRESSOR_EXE} 和 ${DECOMPRESSOR_EXE}"
echo

# 检查可执行文件是否存在
if [ ! -f "${COMPRESSOR_EXE}" ] || [ ! -f "${DECOMPRESSOR_EXE}" ]; then
    echo "!!! 错误: 找不到一个或多个可执行文件。"
    echo "请先编译 ${COMPRESSOR_EXE} 和 ${DECOMPRESSOR_EXE}。"
    exit 1
fi

# 遍历数据集并执行流程
for dataset in "${DATASETS[@]}"; do
    echo "---------------------------------------------"
    echo ">>> 开始处理数据集: ${dataset}"
    
    # 定义此数据集的相关路径
    COMPRESSED_DIR="${RESULTS_BASE_DIR}/${dataset}"
    RESTORED_LOG="${dataset}_restored.log"

    # 1. 运行压缩
    echo "  [1/3] 正在压缩..."
    ${COMPRESSOR_EXE} ${dataset} ${BLOCK_SIZE} ${COMPRESS_MODE} ${THREADS}
    echo "      压缩完成。"

    # 2. 运行解压
    echo "  [2/3] 正在解压..."
    ${DECOMPRESSOR_EXE} "${COMPRESSED_DIR}" "${RESTORED_LOG}" ${THREADS}
    echo "      解压完成，已生成 ${RESTORED_LOG}。"

    # 3. 清理所有过程文件
    echo "  [3/3] 正在清理..."
    # 删除压缩过程产生的所有文件
    if [ -d "${COMPRESSED_DIR}" ]; then
        rm -rf "${COMPRESSED_DIR}"
        echo "      已删除: ${COMPRESSED_DIR}"
    fi
    # 删除解压后生成的日志文件
    if [ -f "${RESTORED_LOG}" ]; then
        rm -f "${RESTORED_LOG}"
        echo "      已删除: ${RESTORED_LOG}"
    fi
    # 以防万一，清理解压器可能留下的临时目录
    if [ -d "denum_decompress_temp" ]; then
        rm -rf "denum_decompress_temp"
        echo "      已删除: denum_decompress_temp"
    fi
    echo "      清理完成。"
    echo "<<< 数据集 ${dataset} 处理完毕。"
    echo
done

echo "============================================="
echo "🎉 所有任务执行完毕！"
echo "============================================="