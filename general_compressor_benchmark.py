import os
import sys
import subprocess
import time
import shutil
import csv
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

# --- CONFIGURATION ---
# All settings for the general-purpose chunked compressor benchmark

CONFIG = {
    # Directory containing all log datasets (e.g., Logs/HDFS, Logs/Zookeeper)
    "LOGS_BASE_DIR": Path("Logs"),

    # A temporary directory to store all intermediate files
    "TEMP_OUTPUT_DIR": Path("general_chunked_benchmark"),

    # Name of the final CSV file for results
    "CSV_RESULTS_FILE": "general_chunked_results.csv",
    
    # Number of parallel threads to use
    "NUM_THREADS": 4,

    # Number of lines per chunk
    "CHUNK_SIZE": 100000,

    # Dictionary of compressors to test
    "COMPRESSORS": {
        "gzip": {
            "ext": ".gz",
            # -f: force overwrite, -k: keep original file
            "compress_cmd": "gzip -f -k {input_file}",
            "decompress_cmd": "gzip -d -f -k {input_file}",
        },
        "bzip2": {
            "ext": ".bz2",
            "compress_cmd": "bzip2 -f -k {input_file}",
            "decompress_cmd": "bzip2 -d -f -k {input_file}",
        },
        "lzma": {
            "ext": ".xz",
            # -f: force, -k: keep, -T 1: use only 1 thread per xz process
            # The script's parallelism comes from running multiple xz processes at once.
            "compress_cmd": "xz -f -k -T 1 {input_file}",
            "decompress_cmd": "xz -d -f -k -T 1 {input_file}",
        }
    }
}

# --- SCRIPT ---

def run_command(command):
    """Executes a shell command and raises an error if it fails."""
    try:
        subprocess.run(command, shell=True, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        print(f"--- ERROR ---")
        print(f"Command failed: {e.cmd}")
        print(f"Stderr: {e.stderr}")
        print(f"---------------")
        raise

def split_log_into_chunks(log_file_path, chunk_dir_path):
    """Splits a large log file into smaller chunks."""
    print(f"  Splitting {log_file_path.name} into chunks...")
    chunk_dir_path.mkdir(parents=True, exist_ok=True)
    
    chunk_files = []
    line_count = 0
    chunk_index = 0
    
    try:
        with open(log_file_path, 'r', encoding='utf-8', errors='ignore') as f_in:
            f_out = None
            for line in f_in:
                if line_count % CONFIG["CHUNK_SIZE"] == 0:
                    if f_out: f_out.close()
                    chunk_path = chunk_dir_path / f"chunk_{chunk_index}.log"
                    chunk_files.append(chunk_path)
                    f_out = open(chunk_path, 'w', encoding='utf-8')
                    chunk_index += 1
                f_out.write(line)
                line_count += 1
            if f_out: f_out.close()
    except Exception as e:
        print(f"Error splitting log file {log_file_path}: {e}")
        raise
            
    print(f"  Split into {len(chunk_files)} chunks.")
    return chunk_files

def compress_chunk(chunk_path, compressor_cmd):
    """Compresses a single chunk file."""
    cmd = compressor_cmd.format(input_file=f"'{chunk_path}'")
    run_command(cmd)
    return chunk_path

def decompress_chunk(compressed_path, decompressor_cmd):
    """Decompresses a single chunk file."""
    cmd = decompressor_cmd.format(input_file=f"'{compressed_path}'")
    run_command(cmd)
    return compressed_path

def log_to_csv(data):
    """Appends a row of data to the results CSV file."""
    file_exists = Path(CONFIG["CSV_RESULTS_FILE"]).exists()
    try:
        with open(CONFIG["CSV_RESULTS_FILE"], 'a', newline='') as f:
            writer = csv.writer(f)
            if not file_exists or f.tell() == 0:
                writer.writerow([
                    "LogName", "Compressor", "OriginalSize_MB", "CompressedSize_MB",
                    "CompressionRate", "CompressionTime_s", "CompressionSpeed_MBps",
                    "DecompressionTime_s", "DecompressionSpeed_MBps"
                ])
            writer.writerow([f"{v:.4f}" if isinstance(v, float) else v for v in data])
    except IOError as e:
        print(f"Error writing to CSV file {CONFIG['CSV_RESULTS_FILE']}: {e}")

def process_dataset(log_dir, temp_dir):
    """Runs the benchmark for a single dataset against all general-purpose compressors in a chunked, parallel fashion."""
    log_name = log_dir.name
    log_file = log_dir / f"{log_name}.log"

    if not log_file.exists():
        print(f"Skipping {log_name}: log file not found at {log_file}")
        return

    print(f"\n===== Processing Dataset: {log_name} (Chunked Parallel) =====")
    
    # Create a dedicated subdir for this dataset's chunks
    chunk_dir = temp_dir / log_name / "chunks"
    
    try:
        # --- SPLIT INTO CHUNKS (Done once per dataset) ---
        chunk_files = split_log_into_chunks(log_file, chunk_dir)
        original_size_bytes = log_file.stat().st_size
        original_size_mb = original_size_bytes / (1024 * 1024)
        print(f"  Original size: {original_size_mb:.4f} MB")

        # --- LOOP OVER COMPRESSORS ---
        for compressor_name, details in CONFIG["COMPRESSORS"].items():
            print(f"  --- Testing with: {compressor_name.upper()} ---")
            
            # --- 1. COMPRESSION (Parallel) ---
            start_time_compress = time.perf_counter()
            with ThreadPoolExecutor(max_workers=CONFIG["NUM_THREADS"]) as executor:
                futures = [executor.submit(compress_chunk, path, details["compress_cmd"]) for path in chunk_files]
                for future in as_completed(futures):
                    future.result() 
            end_time_compress = time.perf_counter()
            compression_time_s = end_time_compress - start_time_compress
            
            # Calculate total compressed size by summing up all individual compressed chunk files
            compressed_chunk_paths = [p.with_suffix(p.suffix + details['ext']) for p in chunk_files]
            total_compressed_size_bytes = sum(p.stat().st_size for p in compressed_chunk_paths)
            compressed_size_mb = total_compressed_size_bytes / (1024 * 1024)
            
            compression_rate = original_size_bytes / total_compressed_size_bytes if total_compressed_size_bytes > 0 else float('inf')
            compression_speed_mbps = original_size_mb / compression_time_s if compression_time_s > 0 else float('inf')
            print(f"    Parallel Compression: {compression_time_s:.4f}s | Speed: {compression_speed_mbps:.4f} MB/s | Rate: {compression_rate:.2f}x")

            # --- 2. DECOMPRESSION (Parallel) ---
            start_time_decompress = time.perf_counter()
            with ThreadPoolExecutor(max_workers=CONFIG["NUM_THREADS"]) as executor:
                futures = [executor.submit(decompress_chunk, path, details["decompress_cmd"]) for path in compressed_chunk_paths]
                for future in as_completed(futures):
                    future.result()
            end_time_decompress = time.perf_counter()
            decompression_time_s = end_time_decompress - start_time_decompress
            decompression_speed_mbps = original_size_mb / decompression_time_s if decompression_time_s > 0 else float('inf')
            print(f"    Parallel Decompression: {decompression_time_s:.4f}s | Speed: {decompression_speed_mbps:.4f} MB/s")

            # --- 3. LOG RESULTS ---
            log_to_csv([
                log_name, compressor_name, original_size_mb, compressed_size_mb,
                compression_rate, compression_time_s, compression_speed_mbps,
                decompression_time_s, decompression_speed_mbps
            ])
            
            # --- 4. CLEANUP FOR THIS COMPRESSOR ---
            # Remove the compressed files to prepare for the next compressor
            for path in compressed_chunk_paths:
                if path.exists(): os.remove(path)
                
    except Exception as e:
        print(f"!!! FAILED to process {log_name}: {e}")
    finally:
        # --- FINAL CLEANUP FOR THIS DATASET ---
        # Clean up the entire directory for this dataset, including the original uncompressed chunks
        dataset_temp_dir = temp_dir / log_name
        if dataset_temp_dir.exists():
            print(f"  Cleaning up all files for {log_name}...")
            shutil.rmtree(dataset_temp_dir)

def main():
    """Main function to find and process all log datasets."""
    # Setup and checks...
    for tool in ["gzip", "bzip2", "xz"]:
        if not shutil.which(tool):
            print(f"Error: Required command-line tool '{tool}' not found in your system's PATH.")
            sys.exit(1)
    
    if not CONFIG["LOGS_BASE_DIR"].is_dir():
        print(f"Error: Base log directory not found at '{CONFIG['LOGS_BASE_DIR']}'")
        sys.exit(1)

    temp_dir = CONFIG["TEMP_OUTPUT_DIR"]
    if temp_dir.exists(): shutil.rmtree(temp_dir)
    temp_dir.mkdir(exist_ok=True)
    
    datasets_to_process = sorted([d for d in CONFIG["LOGS_BASE_DIR"].iterdir() if d.is_dir()])
    if not datasets_to_process:
        print(f"No datasets found in {CONFIG['LOGS_BASE_DIR']}.")
        shutil.rmtree(temp_dir)
        return

    print(f"Found {len(datasets_to_process)} datasets to process with chunked parallel method.")
    
    for log_dir in datasets_to_process:
        process_dataset(log_dir, temp_dir)

    print("\n===== Chunked General-Purpose Benchmark Complete =====")
    print(f"All results saved to {CONFIG['CSV_RESULTS_FILE']}")

if __name__ == "__main__":
    main()