import sys
import os
import argparse
import time
import datetime
import threading
import logging
import subprocess
import json  # <-- 新增：导入 json 模块
from subprocess import call
from concurrent.futures import ThreadPoolExecutor
from os.path import join, getsize

# --- 模拟的 util 模块 ---
try:
    import util
except ImportError:
    class MockUtil:
        def path_pro(self, path):
            return os.path.join(path, '')
    util = MockUtil()
# -------------------------

lock = threading.RLock()
gl_threadTotTime = 0
gl_errorNum = 0

# --- （前面的函数无需修改，保持原样） ---

def atomic_addTime(step):
    lock.acquire()
    global gl_threadTotTime
    gl_threadTotTime += step
    lock.release()

def atomic_addErrnum(step):
    lock.acquire()
    global gl_errorNum
    gl_errorNum += step
    lock.release()

def writeLog(fname, message, levelStr):
    logging.basicConfig(filename=fname,
                           filemode='a',
                           format = '%(asctime)s - %(message)s')
    logger = logging.getLogger(__name__)
    if levelStr == 'WARNING':
        logger.warning(message)
    elif levelStr == 'INFO':
        logger.info(message)

def procFiles(typename, file_indices, now_input, now_output, now_temp, type_template):
    t1 = time.time()
    thread_name = threading.current_thread().name
    thread_temp = join(now_temp, thread_name + "/")
    if os.path.exists(thread_temp):
        call(f"rm -rf {thread_temp}", shell=True, stdout=open(os.devnull, 'w'), stderr=subprocess.STDOUT)
    os.mkdir(thread_temp)

    for i in file_indices:
        try:
            order = f"python3 ./restore.py -I {join(now_input, str(i) + '.7z')} -O {join(now_temp, str(i) + '.col')} -T {type_template} -t {thread_temp}"
            print(f"{order} ({thread_name})")
            res = call(order, shell=True, stdout=open(os.devnull, 'w'), stderr=subprocess.STDOUT)
            if res != 0:
                tempStr = f"Error: restore.py failed for file {i}.7z. Thread: {thread_name}"
                print(tempStr, file=sys.stderr)
                writeLog(str(g_output_path) + "_Log.txt", tempStr, 'WARNING')
                atomic_addErrnum(1)
                continue
        except Exception as e:
            tempStr = f"FATAL Error in thread {thread_name} while processing file {i}: {e}"
            print(tempStr, file=sys.stderr)
            writeLog(str(g_output_path) + "_Log.txt", tempStr, 'WARNING')
            atomic_addErrnum(1)
            continue

    t2 = time.time()
    tempStr = f"Thread:{thread_name}, type:{typename}, processed indices:{file_indices}, cost time: {t2 - t1:.2f}s"
    print(tempStr)
    writeLog(str(g_output_path) + "_Log.txt", tempStr, 'INFO')
    return t2 - t1

def procFiles_result(future):
    if future.exception() is None:
        atomic_addTime(future.result())

def getdirsize(dir_or_file):
    if os.path.isfile(dir_or_file):
        return getsize(dir_or_file)
    size = 0
    for root, _, files in os.walk(dir_or_file):
        size += sum([getsize(join(root, name)) for name in files])
    return size

def calcuReduceRate(inputPath, outputPath, typename):
    inFileSize = getdirsize(inputPath)
    outFileSize = getdirsize(outputPath)
    rate = inFileSize / outFileSize if outFileSize > 0 else 0
    inFileSize_kb = inFileSize / 1024
    outFileSize_kb = outFileSize / 1024
    tempStr = f"Type:{typename}, In(compressed)_Size: {inFileSize_kb:.3f} KB, Out(decompressed)_Size: {outFileSize_kb:.3f} KB, In/Out Ratio: {rate:.3f}"
    print(tempStr)
    writeLog(str(g_output_path) + "_Log.txt", tempStr, 'INFO')

def threadsToExecTasks(typename, files, now_input, now_output, now_temp, type_template, max_threads, files_per_thread):
    try:
        file_map = {int(os.path.splitext(f)[0]): f for f in files}
        sorted_indices = sorted(file_map.keys())
    except (ValueError, IndexError):
        print("Warning: Could not sort input files numerically. Processing in default order.", file=sys.stderr)
        return []

    fileListLen = len(sorted_indices)
    step = files_per_thread
    if step == 0:
        step = (fileListLen + max_threads - 1) // max_threads
        if step == 0:
            step = 1

    threadPool = ThreadPoolExecutor(max_workers=max_threads, thread_name_prefix="LR_Decompress_")
    for i in range(0, fileListLen, step):
        indices_chunk = sorted_indices[i:i + step]
        future = threadPool.submit(procFiles, typename, indices_chunk, now_input, now_output, now_temp, type_template)
        future.add_done_callback(procFiles_result)

    threadPool.shutdown(wait=True)
    return sorted_indices

# --- main 部分 ---
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="A simplified wrapper to restore logs processed by LogReducer.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("log_name", help="The name of the log to restore (e.g., 'Apache', 'Android').")
    parser.add_argument("-TN", "--MaxThreadNum", default="4", help="Max number of threads.")
    args = parser.parse_args()
    log_name = args.log_name

    print(f"--- Preparing to restore log: {log_name} ---")
    input_path = f"./LogReducer_result/{log_name}/out/"
    template_path = f"./LogReducer_result/{log_name}/template/"
    output_path = f"./LogReducer_result/{log_name}/{log_name}_restored.log"
    g_output_path = output_path 

    print(f" -> Compressed Input: {os.path.abspath(input_path)}")
    print(f" -> Template Source:  {os.path.abspath(template_path)}")
    print(f" -> Final Output:     {os.path.abspath(output_path)}")
    
    if not os.path.isdir(input_path):
        print(f"\n[ERROR] Input directory not found: {os.path.abspath(input_path)}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isdir(template_path):
        print(f"\n[ERROR] Template directory not found: {os.path.abspath(template_path)}", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(output_path):
        print(f"Warning: Output file '{output_path}' exists and will be overwritten.")

    maxThreadNum = int(args.MaxThreadNum)
    maxSingleThreadProcFilesNum = 0
    mode = "Tot"
    time1 = time.time()
    
    all_7z_files = [f for f in os.listdir(input_path) if f.endswith(".7z")]
    if not all_7z_files:
        print(f"\n[ERROR] No '.7z' compressed files found in '{input_path}'.", file=sys.stderr)
        sys.exit(1)

    output_dir = os.path.dirname(output_path)
    now_temp = os.path.join(output_dir, f"tmp_{log_name}_restore")
    if os.path.exists(now_temp):
        call(f"rm -rf {now_temp}", shell=True)
    os.makedirs(now_temp, exist_ok=True)

    print(f"\n--- Found {len(all_7z_files)} files to decompress. Starting threads... ---")
    sorted_indices = threadsToExecTasks(
        log_name, all_7z_files, input_path, output_dir, now_temp, template_path,
        maxThreadNum, maxSingleThreadProcFilesNum
    )
    if sorted_indices is None:
        sorted_indices = []

    if mode == "Tot":
        print(f"\n--- Merging temporary files into '{output_path}'... ---")
        try:
            with open(output_path, 'w') as fw:
                for i in sorted_indices:
                    col_file = os.path.join(now_temp, str(i) + ".col")
                    if os.path.exists(col_file):
                        with open(col_file, 'r') as fo:
                            fw.write(fo.read())
                    else:
                        print(f"Warning: Merging skipped for non-existent file: {col_file}")
            print("Merging completed.")
        except (IOError, ValueError) as e:
            print(f"Error during file merging: {e}", file=sys.stderr)
            atomic_addErrnum(1)

    # =============================================================
    # --- 6. 计算、打印并保存结果 (已修改) ---
    # =============================================================
    total_duration_sec = time.time() - time1
    print("\n" + "="*20 + " FINAL RESULTS " + "="*20)

    # --- 初始化指标 ---
    decompressed_size_bytes = 0
    decompressed_size_mb = 0.0
    decompression_speed_mb_s = 0.0

    # --- 计算指标 ---
    try:
        if os.path.exists(output_path) and getsize(output_path) > 0:
            decompressed_size_bytes = getsize(output_path)
            decompressed_size_mb = decompressed_size_bytes / (1024 * 1024)
            decompression_speed_mb_s = decompressed_size_mb / total_duration_sec if total_duration_sec > 0 else 0
            speed_str = f"Decompression Speed: {decompression_speed_mb_s:.3f} MB/s"
            print(speed_str)
            writeLog(g_output_path + "_Log.txt", speed_str, 'INFO')
        else:
            print("Warning: Output file is empty or not found. Speed calculation skipped.")
    except Exception as e:
        print(f"Warning: Error during speed calculation: {e}", file=sys.stderr)

    calcuReduceRate(input_path, output_path, log_name)
    
    summary_str = (
        f"Total time cost: {total_duration_sec:.3f}s, "
        f"Thread accum time: {gl_threadTotTime:.3f}s, "
        f"Error num: {gl_errorNum}"
    )
    print(summary_str)
    writeLog(g_output_path + "_Log.txt", summary_str, 'INFO')
    
    # --- 新增：将结果保存到 JSON 文件 ---
    compressed_input_size_bytes = getdirsize(input_path)
    results_data = {
        "dataset": log_name,
        "total_time_seconds": round(total_duration_sec, 3),
        "compressed_input_size_bytes": compressed_input_size_bytes,
        "compressed_input_size_mb": round(compressed_input_size_bytes / (1024 * 1024), 3),
        "decompressed_output_size_bytes": decompressed_size_bytes,
        "decompressed_output_size_mb": round(decompressed_size_mb, 3),
        "decompression_speed_mb_s": round(decompression_speed_mb_s, 3),
        "thread_accum_time_seconds": round(gl_threadTotTime, 3),
        "error_count": gl_errorNum
    }
    
    results_file_path = os.path.join(output_dir, "decompression_results.json")
    
    try:
        with open(results_file_path, 'w', encoding='utf-8') as f:
            json.dump(results_data, f, indent=4)
        print(f"Detailed decompression results saved to: '{results_file_path}'")
    except IOError as e:
        print(f"\n[ERROR] Could not write results to file '{results_file_path}'. Reason: {e}", file=sys.stderr)

    print("="*52)

    # --- 7. 清理 ---
    if gl_errorNum == 0:
        if os.path.exists(now_temp):
            print(f"Cleaning up temporary directory: {now_temp}")
            call(f"rm -rf {now_temp}", shell=True)
    else:
        print(f"Errors occurred. Temporary directory '{now_temp}' was not deleted for debugging.", file=sys.stderr)