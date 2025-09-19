# -*- coding: utf-8 -*-
"""
de_logshrink.py

A robust wrapper script for LogShrink's decompression process.

This script handles:
1.  Setting up correct paths for the underlying decompression tool.
2.  Applying a necessary workaround for a bug in the tool's template path handling.
3.  Accurately timing ONLY the decompression command.
4.  Calculating decompression speed based on the restored file's size.
5.  Generating a structured JSON result file.
6.  Ensuring cleanup of temporary files, even if errors occur.
"""
import sys
import os
import argparse
import subprocess
import time
import json
import shutil

# --- utils ---
def get_dir_size(directory):
    total_size = 0
    if not os.path.isdir(directory): return 0
    for dirpath, _, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if os.path.isfile(fp): total_size += os.path.getsize(fp)
    return total_size

def get_file_size(file_path):
    if os.path.isfile(file_path): return os.path.getsize(file_path)
    return 0

def run_command_live(command, working_dir=None):
    print("-" * 60)
    print(f"Executing command:")
    print(" ".join(map(str, command)))
    if working_dir: print(f"In Working Directory: {working_dir}")
    print("-" * 60)
    process = subprocess.Popen(
        command, 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT, 
        text=True, 
        encoding='utf-8', 
        errors='ignore', 
        cwd=working_dir
    )
    for line in iter(process.stdout.readline, ''):
        sys.stdout.write(line)
        sys.stdout.flush()
    process.wait()
    return process.returncode

def main():
    # --- 1. parameters defination ---
    parser = argparse.ArgumentParser(
        description="A robust wrapper for LogShrink's decompression.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    # main paramenters
    parser.add_argument("-ds", type=str, required=True, help="Dataset name (e.g., 'Apache')")
    parser.add_argument("-K", type=str, required=True, help="Kernel/Compression method (e.g., 'lzma')")
    parser.add_argument("-E", default='E', type=str, help="Encode mode (default: 'E')")
    
    # path parameters
    parser.add_argument("--logshrink-base", default='./python_compression', help="Base directory of the LogShrink tool.")
    parser.add_argument("--indir-suffix", default='out/out', help="Suffix for compressed data relative to logshrink-base.")
    parser.add_argument("--template-suffix", default='template', help="Suffix for templates relative to logshrink-base.")
    parser.add_argument("--outdir", default='./restore_results', help="Directory to save restored logs and results.")
    
    args = parser.parse_args()

    # --- 2. path processing ---
    logshrink_decompress_dir = os.path.abspath(os.path.join(args.logshrink_base, "decompression"))
    compressed_input_dir = os.path.abspath(os.path.join(args.logshrink_base, args.indir_suffix, args.ds))
    template_dir = os.path.abspath(os.path.join(args.logshrink_base, args.template_suffix, args.ds))
    final_output_dir = os.path.abspath(args.outdir)
    restored_log_path = os.path.join(final_output_dir, f"{args.ds}_restored.log")

    # check path 
    for path in [logshrink_decompress_dir, compressed_input_dir, template_dir]:
        if not os.path.isdir(path):
            print(f"Error: Required directory not found at '{path}'", file=sys.stderr)
            sys.exit(1)
    os.makedirs(final_output_dir, exist_ok=True)

    # --- 3. template restore ---
    temp_template_fix_dir = os.path.join(template_dir, 'out')
    original_template_file = os.path.join(template_dir, 'template.col')
    
    
    if os.path.exists(original_template_file):
        print("Applying workaround for underlying script's template path bug...")
        os.makedirs(temp_template_fix_dir, exist_ok=True)
        temp_template_file_dest = os.path.join(temp_template_fix_dir, 'template.col')
        shutil.copy(original_template_file, temp_template_file_dest)
        print(f"Copied '{original_template_file}' -> '{temp_template_file_dest}'")
    else:
        print(f"Warning: Main template file not found at '{original_template_file}'. The process might fail.", file=sys.stderr)
    
    
    try:
        # --- 4. Decompression ---
    
        rel_input_path = os.path.relpath(compressed_input_dir, logshrink_decompress_dir)
        rel_template_path = os.path.relpath(template_dir, logshrink_decompress_dir)
        rel_output_path = os.path.relpath(restored_log_path, logshrink_decompress_dir)
        
        command = [
            "python3", "decompress_run.py", 
            "-E", args.E, 
            "-K", args.K,
            "-I", rel_input_path, 
            "-T", rel_template_path, 
            "-O", rel_output_path
        ]
        
        # decompression time collection
        start_time = time.time()
        return_code = run_command_live(command, working_dir=logshrink_decompress_dir)
        duration_sec = time.time() - start_time
        
        if return_code != 0:
            print(f"\n[ERROR] LogShrink decompression process failed with exit code {return_code}.", file=sys.stderr)

            
    finally:
        # --- 5. clean process files---
        if os.path.isdir(temp_template_fix_dir):
            print(f"Cleaning up workaround directory: {temp_template_fix_dir}")
            try:
                shutil.rmtree(temp_template_fix_dir)
            except OSError as e:
                print(f"Warning: Failed to cleanup temporary directory. Reason: {e}", file=sys.stderr)

    # --- 6.print/save results---
    print("\n" + "="*20 + " DECOMPRESSION ANALYSIS " + "="*20)

    compressed_size_bytes = get_dir_size(compressed_input_dir)
    decompressed_size_bytes = get_file_size(restored_log_path)
    

    if decompressed_size_bytes > 0 and duration_sec > 0:
        decompression_speed_mb_s = (decompressed_size_bytes / (1024 * 1024)) / duration_sec
    else:
        decompression_speed_mb_s = 0

    print(f"Decompression process finished with exit code {return_code}.")
    print(f"Pure Decompression Time: {duration_sec:.3f} seconds.")
    print("-" * 55)
    print(f"Compressed Input Dir:   '{compressed_input_dir}'")
    print(f"Compressed Size:        {compressed_size_bytes / (1024*1024):.4f} MB")
    print(f"Restored Log File:      '{restored_log_path}'")
    print(f"Restored Size:          {decompressed_size_bytes / (1024*1024):.4f} MB")
    print("-" * 55)
    print(f"Decompression Speed:    {decompression_speed_mb_s:.3f} MB/s")
    
    results_data = {
        "dataset": args.ds,
        "parameters": {"E": args.E, "K": args.K},
        "status": "Success" if return_code == 0 and decompressed_size_bytes > 0 else "Failure",
        "exit_code": return_code,
        "decompression_time_sec": round(duration_sec, 3),
        "compressed_input_path": compressed_input_dir,
        "compressed_size_bytes": compressed_size_bytes,
        "decompressed_output_path": restored_log_path,
        "decompressed_size_bytes": decompressed_size_bytes,
        "decompression_speed_mb_per_s": round(decompression_speed_mb_s, 3),
    }
    
    results_file_path = os.path.join(final_output_dir, f"decompression_{args.ds}_results.json")
    try:
        with open(results_file_path, 'w', encoding='utf-8') as f:
            json.dump(results_data, f, indent=4)
        print("-" * 55)
        print(f"Detailed results saved to: '{results_file_path}'")
    except IOError as e:
        print(f"\n[ERROR] Could not write results file '{results_file_path}'. Reason: {e}", file=sys.stderr)
    print("="*55)
    
    if return_code != 0:
        sys.exit(return_code)

if __name__ == "__main__":
    main()