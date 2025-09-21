#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Function to display help message.
show_help() {
    echo "Usage: docker run --rm -v <local_archives_parent_dir>:/input -v <local_output_dir>:/output <image_name> <input_dir_name> <output_file_name> [threads]"
    echo ""
    echo "This script decompresses an archive created by Delog_compress."
    echo ""
    echo "Arguments:"
    echo "  <input_dir_name>    The name of the directory containing compressed chunks (e.g., 'output_HDFS'). This directory must be inside the volume mounted to /input."
    echo "  <output_file_name>  The name for the final decompressed log file (e.g., 'decompressed_HDFS.log'). It will be saved in the /output volume."
    echo "  [threads]           (Optional) Number of threads to use. Defaults to the number of available cores."
    echo ""
    echo "Example:"
    echo "  # Assuming 'output_HDFS' is inside './my_archives'"
    echo "  docker run --rm -v \$(pwd)/my_archives:/input -v \$(pwd)/my_final_logs:/output delog-decompressor \\"
    echo "    output_HDFS decompressed_HDFS.log 8"
    echo ""
}

# Check for --help flag or an insufficient number of arguments.
if [ "$1" == "--help" ] || [ $# -lt 2 ]; then
    show_help
    exit 0
fi

# Get variables from command-line arguments.
INPUT_DIR_NAME="$1"
OUTPUT_FILE_NAME="$2"
# If the number of threads is not provided, default to the number of available CPU cores.
NUM_THREADS="${3:-$(nproc)}"

# Build the absolute paths inside the container.
FULL_INPUT_PATH="/input/${INPUT_DIR_NAME}"
FULL_OUTPUT_PATH="/output/${OUTPUT_FILE_NAME}"

# Check if the input directory exists.
if [ ! -d "${FULL_INPUT_PATH}" ]; then
    echo "Error: Input directory not found at '${FULL_INPUT_PATH}'."
    echo "Please make sure your directory of archives is in the folder you mounted to /input."
    exit 1
fi

# --- Core Logic ---

echo "========================================"
echo "Starting DeLog Decompression"
echo "========================================"
echo "Source Directory: ${FULL_INPUT_PATH}"
echo "Output File:      ${FULL_OUTPUT_PATH}"
echo "Threads:          ${NUM_THREADS}"
echo "----------------------------------------"

# Execute the C++ decompressor program.
/app/decompress "${FULL_INPUT_PATH}" "${FULL_OUTPUT_PATH}" "${NUM_THREADS}"

echo "----------------------------------------"
echo "Decompression finished successfully."
echo "Output file '${OUTPUT_FILE_NAME}' is now in your host's output directory."
echo "========================================"