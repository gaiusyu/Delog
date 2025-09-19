import sys
import os
import argparse
import subprocess
import time
import json  

def get_dir_size(directory):
 
    total_size = 0
    if not os.path.isdir(directory):
        return 0
    for dirpath, _, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if os.path.isfile(fp):
                total_size += os.path.getsize(fp)
    return total_size

def run_command_live(command, working_dir=None):

    print("-" * 60)
    print(f"Executing command:")
    print(" ".join(map(str, command)))
    if working_dir:
        print(f"Working Directory: {working_dir}")
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

    while True:
        output = process.stdout.readline()
        if output == '' and process.poll() is not None:
            break
        if output:
            print(output.strip())

    return_code = process.poll()
    return return_code

def main():
    # --- Argument Definition ---
 
    parser = argparse.ArgumentParser(
        description="A wrapper script to run LogShrink's compression process.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("-ds", default=None, type=str, required=True, help="Dataset name")
    parser.add_argument("-E", default='E', type=str, help="Encode mode")
    parser.add_argument("-C", action='store_true', help="column mode")
    parser.add_argument("-Dict_c", action='store_true', help="Dict column mode")
    parser.add_argument("-P", action='store_true', help="parsing")
    parser.add_argument("-R", action='store_true', help="random sampling")
    parser.add_argument("-DI", action='store_true', help="diff")
    parser.add_argument("-V", action='store_true', help="variable extraction")
    parser.add_argument("-S", action='store_true', help="sequence sampling")
    parser.add_argument("-I", type=str, help="indir")
    parser.add_argument("-K", type=str, help="kernel")
    parser.add_argument("-L", type=int, default=5, help="header length")
    parser.add_argument("-NC", type=int, default=64, help="sampling candidate")
    parser.add_argument("-TN", type=int, default=4, help="number of workers")
    parser.add_argument("-wh", type=int, default=20, help="windows length")
    parser.add_argument("-th", type=int, default=5, help="clustering distance threshold")
    parser.add_argument("-outdir", default='./out', type=str, help="out dir")
    parser.add_argument("-mt", type=float, default=0.5, help="multiplicity threshold")

    args = parser.parse_args()

    # --- Path Handling ---

    logshrink_run_dir = os.path.join("./", "python_compression")

    if not os.path.isdir(logshrink_run_dir):
        print(f"Error: Directory '{logshrink_run_dir}' not found. Please ensure this script is in the same directory as the 'python_compression' folder.", file=sys.stderr)
        sys.exit(1)

    input_dir_abs = os.path.abspath(args.I)
    output_dir_abs = os.path.abspath(args.outdir)

    if not input_dir_abs.endswith(os.path.sep):
        input_dir_abs += os.path.sep

    original_log_file = os.path.join(os.path.abspath(args.I), args.ds, f"{args.ds}.log")

    if not os.path.exists(original_log_file):
        print(f"Error: Input log file not found at '{original_log_file}'", file=sys.stderr)
        sys.exit(1)

    try:
        original_size_bytes = os.path.getsize(original_log_file)
    except OSError as e:
        print(f"Error: Cannot get size of input file '{original_log_file}'. Reason: {e}", file=sys.stderr)
        sys.exit(1)

    # --- Build and Execute Command ---
    # (Unchanged)
    total_start_time = time.time()

    command = [
        "python3", "run.py",
        "-ds", args.ds, "-E", args.E, "-I", input_dir_abs, "-K", args.K,
        "-L", str(args.L), "-NC", str(args.NC), "-TN", str(args.TN),
        "-wh", str(args.wh), "-th", str(args.th), "-outdir", output_dir_abs,
        "-mt", str(args.mt)
    ]
    if args.C: command.append("-C")
    if args.Dict_c: command.append("-Dict_c")
    if args.P: command.append("-P")
    if args.R: command.append("-R")
    if args.DI: command.append("-DI")
    if args.V: command.append("-V")
    if args.S: command.append("-S")

    return_code = run_command_live(command, working_dir=logshrink_run_dir)

    if return_code != 0:
        print(f"\n[ERROR] LogShrink process failed with exit code {return_code}.", file=sys.stderr)
        sys.exit(1)

    total_end_time = time.time()
    total_duration_sec = total_end_time - total_start_time

    # =============================================================
    # --- Calculate, Print, and Save Results ---
    # =============================================================
    print("\n" + "="*20 + " FINAL RESULTS " + "="*20)

    # --- Calculate Metrics ---
    final_output_path = os.path.join(output_dir_abs, args.ds)
    compressed_size_bytes = get_dir_size(final_output_path)
    original_size_mb = original_size_bytes / (1024 * 1024)
    compressed_size_mb = compressed_size_bytes / (1024 * 1024)
    compression_ratio = original_size_bytes / compressed_size_bytes if compressed_size_bytes > 0 else 0
    compression_speed_mb_s = original_size_mb / total_duration_sec if total_duration_sec > 0 else 0

    # --- Print to Console (Unchanged) ---
    print("LogShrink process completed successfully!")
    print(f"Total time taken: {total_duration_sec:.3f} seconds.")
    print("-" * 52)
    print(f"Original Log: '{original_log_file}'")
    print(f"Original Size: {original_size_bytes} bytes ({original_size_mb:.3f} MB)")
    print(f"Compressed Output: '{final_output_path}'")
    print(f"Compressed Size: {compressed_size_bytes} bytes ({compressed_size_mb:.3f} MB)")
    print("-" * 52)
    print(f"Compression Ratio: {compression_ratio:.3f} : 1")
    print(f"Compression Speed: {compression_speed_mb_s:.3f} MB/s")

    # --- Save results to a JSON file ---
    results_data = {
        "dataset": args.ds,
        "total_time_seconds": round(total_duration_sec, 3),
        "original_file_path": original_log_file,
        "original_size_bytes": original_size_bytes,
        "original_size_mb": round(original_size_mb, 3),
        "compressed_output_path": final_output_path,
        "compressed_size_bytes": compressed_size_bytes,
        "compressed_size_mb": round(compressed_size_mb, 3),
        "compression_ratio": round(compression_ratio, 3),
        "compression_speed_mb_s": round(compression_speed_mb_s, 3),
        "parameters": vars(args)  # Save all input parameters for traceability
    }

    # Ensure the final output directory exists
    os.makedirs(final_output_path, exist_ok=True)
    results_file_path = os.path.join(final_output_path, "compression_results.json")

    try:
        with open(results_file_path, 'w', encoding='utf-8') as f:
            # Use json.dump to write the dictionary to a file, indent=4 for readability
            json.dump(results_data, f, indent=4)
        print("-" * 52)
        print(f"Detailed results have been saved to: '{results_file_path}'")
    except IOError as e:
        print(f"\n[ERROR] Could not write results to file '{results_file_path}'. Reason: {e}", file=sys.stderr)

    print("="*52)


if __name__ == "__main__":
    main()