#!/bin/bash

# 发生错误时立即退出
set -e

# 显示帮助信息的函数
show_help() {
    echo "Usage: docker run --rm -v <local_log_dir>:/data -v <local_output_dir>:/output <image_name> [OPTIONS] <input_file> <log_name>"
    echo ""
    echo "This script compresses a log file using the Delog_compress algorithm."
    echo ""
    echo "Arguments:"
    echo "  <input_file>        The name of the log file to compress, located inside the /data volume."
    echo "  <log_name>          A logical name for the log type (e.g., HDFS, Apache). Used by the compressor."
    echo ""
    echo "Options (must match the C++ program's arguments):"
    echo "  --log-mode <mode>         'text' or 'json'. Default: text"
    echo "  --block-size <num>        Lines per chunk. Default: 100000"
    echo "  --threads <num>           Number of threads. Default: 4"
    echo "  --threshold <num>         Frequency threshold. Default: 0"
    echo "  --kernel <name>           Compression kernel (lzma, gzip, bzip2, lz4, none). Default: lzma"
    echo "  --processing-mode <mode>  'normal' or 'fast'. Default: normal"
    echo "  --keep-temp-files         Keep intermediate files after compression."
    echo ""
    echo "Example:"
    echo "  docker run --rm -v \$(pwd)/my_logs:/data -v \$(pwd)/my_output:/output my-compressor-image \\"
    echo "    my_access.log Apache --kernel gzip --threads 8"
    echo ""
}

# 默认参数
LOG_MODE="text"
BLOCK_SIZE="100000"
NUM_THREADS="4"
FREQ_THRESHOLD="0"
KERNEL="lzma"
PROCESSING_MODE="normal"
KEEP_TEMP_FILES=""
ARGS=()

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --log-mode)
            LOG_MODE="$2"; shift 2 ;;
        --block-size)
            BLOCK_SIZE="$2"; shift 2 ;;
        --threads)
            NUM_THREADS="$2"; shift 2 ;;
        --threshold)
            FREQ_THRESHOLD="$2"; shift 2 ;;
        --kernel)
            KERNEL="$2"; shift 2 ;;
        --processing-mode)
            PROCESSING_MODE="$2"; shift 2 ;;
        --keep-temp-files)
            KEEP_TEMP_FILES="--keep-temp-files"; shift 1 ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

if [ ${#ARGS[@]} -ne 2 ]; then
    echo "Error: Missing <input_file> and/or <log_name>."
    echo ""
    show_help
    exit 1
fi

INPUT_FILE="${ARGS[0]}"
LOG_NAME="${ARGS[1]}"
INPUT_FILE_PATH="/data/${INPUT_FILE}"


if [ ! -f "${INPUT_FILE_PATH}" ]; then
    echo "Error: Input file not found at '${INPUT_FILE_PATH}'."
    echo "Please make sure your log file is in the directory you mounted to /data."
    exit 1
fi


INTERNAL_LOG_DIR="/app/Logs/${LOG_NAME}"
mkdir -p "${INTERNAL_LOG_DIR}"

ln -s "${INPUT_FILE_PATH}" "${INTERNAL_LOG_DIR}/${LOG_NAME}.log"

echo "=========================================================="
echo "Starting Delog Compression"
echo "=========================================================="
echo "Input File:      ${INPUT_FILE}"
echo "Log Name:        ${LOG_NAME}"
echo "Log Mode:        ${LOG_MODE}"
echo "Processing Mode: ${PROCESSING_MODE}"
echo "Kernel:          ${KERNEL}"
echo "Threads:         ${NUM_THREADS}"
echo "----------------------------------------------------------"


/app/Delog_compress \
    "${LOG_NAME}" \
    "${LOG_MODE}" \
    "${BLOCK_SIZE}" \
    "${NUM_THREADS}" \
    "${FREQ_THRESHOLD}" \
    "${KERNEL}" \
    "${PROCESSING_MODE}" \
    ${KEEP_TEMP_FILES}

echo "----------------------------------------------------------"
echo "Compression finished."

INTERNAL_OUTPUT_DIR="/app/output/${LOG_NAME}"
HOST_OUTPUT_DIR="/output/output_${LOG_NAME}"

# 
echo "Moving final archive(s) to the /output volume..."
mkdir -p "${HOST_OUTPUT_DIR}"
find "${HOST_OUTPUT_DIR}" -maxdepth 1 -name "chunk_*.tar*" -delete
# 
find "${INTERNAL_OUTPUT_DIR}" -name "chunk_*.tar*" -exec mv {} "${HOST_OUTPUT_DIR}/" \;

echo "Done. The compressed file(s) are now in your host output directory: output_${LOG_NAME}/"
echo "=========================================================="

# 
if [ -f "/app/experiment_results.csv" ]; then
    mv /app/experiment_results.csv /output/
    echo "Experiment results log moved to /output/experiment_results.csv"
fi
