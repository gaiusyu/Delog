import os
import sys
import time
import re
import gzip
import brotli
import lz4.frame
import argparse
import shutil
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from contextlib import nullcontext

# ===================================================================
# Helper Functions
# ===================================================================

def format_bytes(size):
    """Formats bytes into a more readable format (KB, MB, GB)."""
    if size == 0:
        return "0 B"
    power = 1024
    n = 0
    power_labels = {0: 'B', 1: 'KB', 2: 'MB', 3: 'GB', 4: 'TB'}
    while size >= power and n < len(power_labels) - 1:
        size /= power
        n += 1
    return f"{size:.2f} {power_labels[n]}"

def get_decompressor(algorithm_name):
    """Returns a decompression function based on the algorithm name."""
    decompressors = {
        'gzip': gzip.decompress,
        'brotli': brotli.decompress,
        'lz4': lz4.frame.decompress
    }
    if algorithm_name.lower() not in decompressors:
        raise ValueError(f"Unsupported algorithm: {algorithm_name}. Options are: gzip, brotli, lz4")
    return decompressors[algorithm_name.lower()]

def natural_sort_key(s):
    """Key for natural sorting of filenames, e.g., 'chunk2' comes before 'chunk10'."""
    return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', s)]

# ===================================================================
# Core Worker Function (Decompress to Temp File)
# ===================================================================

def decompress_chunk_to_temp_file(chunk_path, decompress_func, temp_dir):
    """
    Decompresses a single data chunk to a temporary file and returns metadata and the temp file path.
    This function is executed by a child process and does not return large data objects to avoid IPC bottlenecks.
    """
    start_time = time.perf_counter()
    with open(chunk_path, 'rb') as f:
        compressed_data = f.read()

    decompressed_data = decompress_func(compressed_data)
    duration = time.perf_counter() - start_time
    
    chunk_id_match = re.search(r'chunk(\d+)', os.path.basename(chunk_path))
    if not chunk_id_match:
        raise ValueError(f"Cannot parse chunk ID from filename '{os.path.basename(chunk_path)}'.")
    chunk_id = int(chunk_id_match.group(1))

    # Create a unique temporary file to save the decompressed data
    temp_file_path = os.path.join(temp_dir, f"chunk_{chunk_id}.part")
    with open(temp_file_path, 'wb') as f_temp:
        f_temp.write(decompressed_data)

    # Return only metadata and the path, not the data itself
    return {
        'chunk_id': chunk_id,
        'compressed_size': len(compressed_data),
        'decompressed_size': len(decompressed_data),
        'duration': duration,
        'temp_path': temp_file_path 
    }

# ===================================================================
# Main Flow Control (Final Fixed Version)
# ===================================================================

def stream_decompress_pipeline_fixed(input_dir, output_filepath, num_threads, algorithm):
    """
    Main function using the 'worker process writes to temp file' pattern, correctly handling the /dev/null case.
    """
    # 1. Parameter validation and preparation
    try:
        decompress_func = get_decompressor(algorithm)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if not os.path.isdir(input_dir):
        print(f"Error: Input directory not found '{input_dir}'", file=sys.stderr)
        sys.exit(1)

    extension_map = { 'gzip': '.gz', 'brotli': '.br', 'lz4': '.lz4' }
    extension = extension_map[algorithm]
    chunk_files = [os.path.join(input_dir, f) for f in os.listdir(input_dir) if f.endswith(extension)]
    
    if not chunk_files:
        print(f"Error: No chunk files with extension '{extension}' found in '{input_dir}'.", file=sys.stderr)
        sys.exit(1)

    chunk_files.sort(key=lambda f: natural_sort_key(os.path.basename(f)))
    total_chunks = len(chunk_files)
    total_compressed_size = sum(os.path.getsize(f) for f in chunk_files)

    print("=" * 60)
    print(f"Starting parallel decompression (final fixed version): {os.path.basename(input_dir)}")
    print(f"Will output to file: {output_filepath}")
    print(f"Total size of compressed chunks: {format_bytes(total_compressed_size)}")
    print(f"Number of chunk files found: {len(chunk_files)}")
    print(f"Using threads: {num_threads}")
    print(f"Decompression algorithm: {algorithm.upper()}")
    print("=" * 60)

    # ==================== Core Fix Point ====================
    temp_dir_base = None # Default to using the system temporary directory (e.g., /tmp)
    # Check if the output path is /dev/null (case-insensitive and path-separator-insensitive)
    is_dev_null = os.path.normpath(output_filepath).lower() == os.path.normpath('/dev/null').lower()

    if not is_dev_null:
        # Optimization: If the output is a real file, create the temporary directory on the same filesystem
        # to allow for fast moves later when concatenating files, rather than cross-partition copies.
        temp_dir_base = os.path.dirname(os.path.abspath(output_filepath))
        # Ensure the directory exists
        os.makedirs(temp_dir_base, exist_ok=True)
    
    # Use tempfile to create a secure, unique temporary directory
    temp_dir = tempfile.mkdtemp(prefix='decompress_parts_', dir=temp_dir_base)
    # ======================================================
    
    print(f"Temporary files will be stored in: {temp_dir}")

    total_decompressed_size = 0
    main_start_time = time.perf_counter()

    try:
        results_buffer = {} 
        next_chunk_to_write = 0

        # Determine the file opening context based on whether writing to /dev/null
        f_out_context = open(output_filepath, 'wb') if not is_dev_null else nullcontext()

        with ProcessPoolExecutor(max_workers=num_threads) as executor, f_out_context as f_out:
            
            futures = {
                executor.submit(decompress_chunk_to_temp_file, path, decompress_func, temp_dir): path 
                for path in chunk_files
            }
            
            print("All chunks have been dispatched, waiting for decompression and writing to temp files...")
            for future in as_completed(futures):
                try:
                    result = future.result()
                    chunk_id = result['chunk_id']
                    
                    print(f" [Chunk {result['chunk_id']}] Decompressed to temp file. "
                          f"({format_bytes(result['compressed_size'])}) -> "
                          f"{format_bytes(result['decompressed_size'])}, "
                          f"Time {result['duration']:.3f}s. Waiting to be concatenated...")

                    results_buffer[chunk_id] = result['temp_path']
                    total_decompressed_size += result['decompressed_size']
                    
                    while next_chunk_to_write in results_buffer:
                        temp_path_to_write = results_buffer.pop(next_chunk_to_write)
                        
                        if not is_dev_null:
                            # Only actually perform concatenation if the output is not /dev/null
                            with open(temp_path_to_write, 'rb') as f_temp:
                                shutil.copyfileobj(f_temp, f_out)
                        
                        os.remove(temp_path_to_write)
                        
                        if not is_dev_null:
                            print(f" --> [Chunk {next_chunk_to_write}] Concatenated and temp file deleted.")
                        else:
                            print(f" --> [Chunk {next_chunk_to_write}] Processed and temp file deleted.")
                        
                        next_chunk_to_write += 1
                
                except Exception as e:
                    path = futures[future]
                    print(f"Processing chunk '{path}' failed: {e}", file=sys.stderr)
            
            if next_chunk_to_write != total_chunks:
                 print("\nWarning: Not all chunks were written. There may have been failed chunks.", file=sys.stderr)

    finally:
        # Ensure an attempt is made to clean up the temporary directory, regardless of success or failure
        if os.path.exists(temp_dir):
            print(f"Cleaning up temporary directory: {temp_dir}")
            shutil.rmtree(temp_dir)

    main_end_time = time.perf_counter()

    # 3. Final Summary
    print("-" * 60)
    print("Decompression Summary:")
    print(f"  Total chunks processed: {total_chunks}")
    print(f"  Total size of compressed chunks: {format_bytes(total_compressed_size)}")
    print(f"  Total decompressed size: {format_bytes(total_decompressed_size)}")
    if total_compressed_size > 0:
        overall_ratio = total_decompressed_size / total_compressed_size
        print(f"  Overall compression ratio (approximate): {overall_ratio:.2f} : 1")
    print(f"  Total time taken: {main_end_time - main_start_time:.2f} seconds")
    if not is_dev_null:
        print(f"  Full file saved to: '{output_filepath}'")
    else:
        print(f"  Output was discarded to /dev/null.")
    print("-" * 60)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Decompresses in parallel using a 'worker process writes to temp file' pattern for stable memory usage, with correct handling for /dev/null.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("input_dir", help="Directory containing the compressed chunk files.")
    parser.add_argument("output_filepath", help="Output file path for the concatenated decompressed result. Use /dev/null to perform a performance benchmark only.")
    parser.add_argument(
        "-t", "--threads",
        type=int,
        default=os.cpu_count(),
        help=f"Number of parallel threads/processes for decompression. Default: System CPU core count ({os.cpu_count()})"
    )
    parser.add_argument(
        "-a", "--algorithm",
        type=str,
        required=True,
        choices=['gzip', 'brotli', 'lz4'],
        help="Decompression algorithm. Must match the algorithm used for compression."
    )
    
    args = parser.parse_args()
    stream_decompress_pipeline_fixed(args.input_dir, args.output_filepath, args.threads, args.algorithm)