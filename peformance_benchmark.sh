#!/bin/bash

# ==============================================================================
# Parallel Block Compression Benchmark Script
#
# This script measures the performance of compressing a large log file using
# a parallel, block-based strategy. The process is as follows:
#   1. The input log file is split into smaller chunks (blocks).
#   2. The chunks are compressed in parallel using multiple threads.
#
# It uses the GNU `time` command to measure the total performance of this
# entire pipeline (splitting + parallel compression).
# ==============================================================================

# --- Configuration ---
# Directory where performance statistics will be saved
OUTPUT_DIR="benchmark_results"
# Temporary directory for file chunks. It will be created and cleaned up automatically.
TEMP_DIR="${OUTPUT_DIR}/temp_chunks"

# --- Benchmark Parameters ---
INPUT_FILE="Logs/bytedance_C/bytedance_C.log"
LINES_PER_BLOCK=100000
NUM_THREADS=4
COMPRESSION_TOOL="xz -9" # -9 for highest compression. xz uses the LZMA algorithm.

# --- Script Core ---

# Function to run a benchmark for a given command string
# $1: The name of the test
# $2: The command string to execute
run_test() {
    local test_name=$1
    local command_string=$2
    local stats_file="${OUTPUT_DIR}/${test_name}_stats.txt"

    echo "--- [START] Benchmarking: ${test_name} ---"
    echo "Using ${NUM_THREADS} threads to compress in ${LINES_PER_BLOCK}-line blocks."

    # Use /usr/bin/time to measure the performance of the entire command string
    # The command string is executed within a new bash shell (`bash -c "..."`)
    /usr/bin/time -v -o "$stats_file" bash -c "$command_string"

    if [ $? -eq 0 ]; then
        echo "Success. Statistics saved to: ${stats_file}"
    else
        echo "Error: Command failed for '${test_name}'. Check logs."
        echo "Partial statistics may be available in ${stats_file}"
    fi

    echo "--- [END] Benchmarking: ${test_name} ---"
    echo ""
}

# Function to display the result of a single test
show_result() {
    local test_name=$1
    local stats_file="${OUTPUT_DIR}/${test_name}_stats.txt"

    echo "=========================================================================="
    echo "               Benchmark Result for: ${test_name}"
    echo "=========================================================================="

    if [[ -f "$stats_file" ]]; then
        cat "$stats_file"
    else
        echo "Statistics file not found: ${stats_file}"
    fi

    echo "=========================================================================="
}


# --- Main Execution ---

echo "Starting performance benchmark..."
echo "Results will be stored in the '${OUTPUT_DIR}' directory."
echo ""

# 1. Check for required tools
for tool in /usr/bin/time split xargs xz; do
    if ! command -v $tool &> /dev/null; then
        echo "Error: Required command '$tool' not found." >&2
        exit 1
    fi
done

# 2. Check if the input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file not found at '$INPUT_FILE'" >&2
    exit 1
fi

# 3. Create necessary directories
mkdir -p "$OUTPUT_DIR"
mkdir -p "$TEMP_DIR"

# Ensure the temporary directory is cleaned up on script exit (even on error)
trap "echo 'Cleaning up temporary files...'; rm -rf '$TEMP_DIR'" EXIT

# 4. Define the full pipeline command to be benchmarked
#    - Step 1: Split the large file into chunks in the temp directory.
#    - Step 2: Use `find` to get all chunk files and pipe them to `xargs`.
#    - Step 3: `xargs` runs `xz` in parallel (-P) on the chunks.
#              Output of xz is discarded (> /dev/null) to measure pure CPU performance.
BENCHMARK_CMD="
    echo 'Step 1: Splitting file...';
    split -l ${LINES_PER_BLOCK} '${INPUT_FILE}' '${TEMP_DIR}/chunk_';
    
    echo 'Step 2: Compressing chunks in parallel...';
    find '${TEMP_DIR}' -type f -name 'chunk_*' | xargs -P ${NUM_THREADS} -I {} ${COMPRESSION_TOOL} -c {} > /dev/null;
    
    echo 'Compression finished.';
"

# 5. Run the benchmark
TEST_NAME="Parallel_LZMA"
run_test "$TEST_NAME" "$BENCHMARK_CMD"

# 6. Show the final result
show_result "$TEST_NAME"

echo "Benchmark complete."