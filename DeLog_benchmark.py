#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import subprocess
import shutil
import time

# ==============================================================================
# ==                      CONFIGURE YOUR PARAMETERS HERE                      ==
# ==============================================================================

# Thresholds for each dataset.
DATASET_THRESHOLDS = {
    'Apache': 0,
    # 'Android': 0, # Example: Add other datasets here
}

# Paths to the C++ executables and the verification script.
EXECUTABLES = {
    'compressor': './Delog_compress',
    'decompressor': './decompress'
}
VERIFIER_SCRIPT = './diff_compare.py' # Path to your comparison script

# Directory configurations.
LOG_INPUT_DIR = './Logs'
COMPRESSED_OUTPUT_DIR = './output'
DECOMPRESSED_OUTPUT_DIR = './restored_logs'

# Shared parameters for the pipeline.
PIPELINE_PARAMS = {
    'data_type': 'text',
    'batch_size': 100000,
    'threads': 4,
    'compress_mode': 'lzma',
    'log_type': 'normal'
}
# ==============================================================================


def execute_command(command, step_name, dataset_name):
    """
    Executes a command in real-time, prints its output, and returns its success status.
    (This function does not need modification)
    """
    print("\n" + "="*20 + f" {step_name.upper()} for '{dataset_name}' " + "="*20)
    # The ' '.join is for display purposes only. It implicitly converts all elements to strings for printing.
    print(f"Command: {' '.join(map(str, command))}")
    print("-" * (42 + len(step_name) + len(dataset_name)))
    
    start_time = time.time()
    try:
        # For subprocess.Popen, the command must be a list of strings.
        # The original script correctly converts numeric params to strings before calling this.
        process = subprocess.Popen(
            [str(c) for c in command], # Ensure all parts of the command are strings
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
            errors='ignore'
        )
        
        # Stream output in real-time.
        for line in iter(process.stdout.readline, ''):
            sys.stdout.write(line)
            sys.stdout.flush()
            
        process.wait()
        return_code = process.returncode
        
    except FileNotFoundError:
        print(f"\n[FATAL ERROR] Command not found: '{command[0]}'. Is it in the correct path and executable?", file=sys.stderr)
        return False
    except Exception as e:
        print(f"\n[FATAL ERROR] An unexpected error occurred: {e}", file=sys.stderr)
        return False

    duration = time.time() - start_time
    print("-" * (42 + len(step_name) + len(dataset_name)))

    if return_code != 0:
        print(f"[FAILURE] {step_name} for '{dataset_name}' failed with exit code {return_code} after {duration:.2f}s.", file=sys.stderr)
        return False
    else:
        print(f"[SUCCESS] {step_name} for '{dataset_name}' completed in {duration:.2f}s.")
        return True

def cleanup_process_files(dataset_name):
    """
    Cleans up the intermediate and output files generated for a specific dataset.
    (This function does not need modification)
    """
    print(f"\n--- Cleaning up files for '{dataset_name}' ---")
    
    compressed_dir = os.path.join(COMPRESSED_OUTPUT_DIR, dataset_name)
    decompressed_file = os.path.join(DECOMPRESSED_OUTPUT_DIR, f"decompressed_{dataset_name}.log")

    if os.path.isdir(compressed_dir):
        try:
            shutil.rmtree(compressed_dir)
            print(f"Removed directory: {compressed_dir}")
        except OSError as e:
            print(f"Error removing directory {compressed_dir}: {e}", file=sys.stderr)
    else:
        print(f"Warning: Directory to clean not found: {compressed_dir}")
        
    if os.path.isfile(decompressed_file):
        try:
            os.remove(decompressed_file)
            print(f"Removed file: {decompressed_file}")
        except OSError as e:
            print(f"Error removing file {decompressed_file}: {e}", file=sys.stderr)
    else:
        print(f"Warning: File to clean not found: {decompressed_file}")
    
    print("Cleanup finished.")

def main():
    """Main execution function for the pipeline."""
    # --- Initial Checks ---
    for name, path in EXECUTABLES.items():
        if not os.path.exists(path):
            print(f"Error: {name} executable not found at '{path}'. Aborting.", file=sys.stderr)
            sys.exit(1)
            
    if not os.path.exists(VERIFIER_SCRIPT):
        print(f"Error: Verifier script not found at '{VERIFIER_SCRIPT}'. Aborting.", file=sys.stderr)
        sys.exit(1)
    
    if not os.path.isdir(LOG_INPUT_DIR):
        print(f"Error: Log input directory not found at '{LOG_INPUT_DIR}'. Aborting.", file=sys.stderr)
        sys.exit(1)

    # --- Setup Directories ---
    os.makedirs(COMPRESSED_OUTPUT_DIR, exist_ok=True)
    os.makedirs(DECOMPRESSED_OUTPUT_DIR, exist_ok=True)
    
    datasets_to_run = list(DATASET_THRESHOLDS.keys())
    total_datasets = len(datasets_to_run)
    
    print("#"*80)
    print(f"#  Starting Compression/Decompression/Verification Pipeline for {total_datasets} datasets  #")
    print("#"*80)
    
    for i, dataset_name in enumerate(datasets_to_run):
        print("\n" + "#"*30 + f" PROCESSING: {dataset_name.upper()} ({i+1}/{total_datasets}) " + "#"*30)
        
        threshold_param = DATASET_THRESHOLDS[dataset_name]
        
        # --- Step 1: Compression ---
        compress_cmd = [
            EXECUTABLES['compressor'],
            dataset_name,
            PIPELINE_PARAMS['data_type'],
            PIPELINE_PARAMS['batch_size'],
            PIPELINE_PARAMS['threads'],
            threshold_param,
            PIPELINE_PARAMS['compress_mode'],
            PIPELINE_PARAMS['log_type']
        ]
        
        if not execute_command(compress_cmd, "Compression", dataset_name):
            print(f"Skipping rest of the pipeline for '{dataset_name}' due to compression failure.")
            cleanup_process_files(dataset_name)
            continue

        # --- Step 2: Decompression ---
        compressed_path = os.path.join(COMPRESSED_OUTPUT_DIR, dataset_name)
        decompressed_path = os.path.join(DECOMPRESSED_OUTPUT_DIR, f"decompressed_{dataset_name}.log")
        
        decompress_cmd = [
            EXECUTABLES['decompressor'],
            compressed_path,
            decompressed_path,
            PIPELINE_PARAMS['threads']
        ]
        
        if execute_command(decompress_cmd, "Decompression", dataset_name):
            # --- Step 3: Verification (New) ---
            # This step runs only if decompression was successful.
            original_log_path = os.path.join(LOG_INPUT_DIR, f"{dataset_name}/{dataset_name}.log")

            if not os.path.isfile(original_log_path):
                print(f"\n[ERROR] Original log file not found for verification: {original_log_path}", file=sys.stderr)
            else:
                verify_cmd = [
                    'python3',
                    VERIFIER_SCRIPT,
                    dataset_name,
                    '-m', '1'
                ]
                # The result of this command indicates if the compression was lossless.
                execute_command(verify_cmd, "Verification (Lossless Check)", dataset_name)
        else:
            print(f"Decompression failed for '{dataset_name}'. Skipping verification.")

        # --- Step 4: Cleanup ---
        cleanup_process_files(dataset_name)

    print("\n" + "#"*80)
    print("#  Pipeline completed for all datasets. All intermediate files are cleaned.  #")
    print("#"*80)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nPipeline interrupted by user. Exiting.")
        sys.exit(1)