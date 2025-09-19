#!/bin/bash
set -e # Exit immediately if a command exits with a non-zero status.

# --- Configuration Area ---
LOG_BASE_NAME="bytedance"
LOG_FILE_PATH="Logs/${LOG_BASE_NAME}/${LOG_BASE_NAME}.log"
LINE_COUNT=100000
THREADS=4
OUTPUT_DIR="benchmark_results_cpu"

# --- Script Core ---

# Check if required commands and files exist
for cmd in python3 /usr/bin/time ./tag_processor; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "Error: Command not found: $cmd"
        echo "Please ensure python3, /usr/bin/time, and your ./tag_processor program are in your PATH or the current directory."
        exit 1
    fi
done

if [ ! -f "general_compressor.py" ]; then
    echo "Error: Python compression script 'general_compressor.py' not found."
    exit 1
fi

if [ ! -f "$LOG_FILE_PATH" ]; then
    echo "Error: Log file not found at: $LOG_FILE_PATH"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
echo "CPU/Memory performance test results will be saved in the '${OUTPUT_DIR}' directory."
echo "All compression output will be redirected to /dev/null to focus on computational performance."
echo ""

# run_test function
# Argument 1: Test name
# Argument 2...: The full command to execute
run_test() {
    local test_name=$1
    shift
    local cmd_to_run=("$@")

    echo "--- [START] Test: ${test_name} ---"

    # Redirect the command's standard output to /dev/null
    # The statistics from 'time' are output to a file via the -o parameter
    /usr/bin/time -v -o "${OUTPUT_DIR}/${test_name}_stats.txt" "${cmd_to_run[@]}" > /dev/null

    echo "Statistics saved to: ${OUTPUT_DIR}/${test_name}_stats.txt"
    echo "--- [END] Test: ${test_name} ---"
    echo ""
}

# --- Execute All Tests ---

# 1. Test your custom algorithm 'tag_processor'
# Assuming it prints results to standard output. May need adjustment if it forces file creation.
run_test "MyAlgorithm_LZMA" \
    ./Delog_compress "$LOG_BASE_NAME" text "$LINE_COUNT" "$THREADS" 100 lzma fast

# 2. Test Brotli
run_test "Brotli" \
    python3 general_compressor.py "$LOG_FILE_PATH" -a brotli -t "$THREADS" -l "$LINE_COUNT"

# 3. Test Gzip
run_test "Gzip" \
    python3 general_compressor.py "$LOG_FILE_PATH" -a gzip -t "$THREADS" -l "$LINE_COUNT"

# 4. Test LZ4
run_test "LZ4" \
    python3 general_compressor.py "$LOG_FILE_PATH" -a lz4 -t "$THREADS" -l "$LINE_COUNT"

echo "All CPU/Memory benchmarks are complete!"