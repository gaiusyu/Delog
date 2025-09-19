import sys
import os
import argparse
import subprocess
import time
import json

def get_dir_size(directory):
    """
    计算目录的总大小。
    (此函数无需修改)
    """
    total_size = 0
    for dirpath, _, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if os.path.isfile(fp):
                total_size += os.path.getsize(fp)
    return total_size

# ##### 修改部分 1: run_command 函数 #####
# 让函数返回一个包含成功状态和执行时长的元组 (success, duration)
def run_command(command, step_name, working_dir=None):
    """
    执行一个shell命令，检查其返回状态，并返回成功状态和执行时长。
    """
    print("-" * 50)
    print(f"[{step_name}] Executing command:")
    print(" ".join(command))
    if working_dir:
        print(f"Working Directory: {working_dir}")
    print("-" * 50)
    start_time = time.time()
    
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding='utf-8',
        errors='ignore',
        cwd=working_dir
    )
    
    end_time = time.time()
    duration = end_time - start_time
    
    if result.stdout:
        print(f"[{step_name}] STDOUT:\n{result.stdout.strip()}")
    if result.stderr:
        print(f"[{step_name}] STDERR:\n{result.stderr.strip()}", file=sys.stderr)
        
    if result.returncode != 0:
        print(f"\n[ERROR] {step_name} step failed with exit code {result.returncode}.", file=sys.stderr)
        return False, duration  # <-- 修改：返回失败状态和时长
        
    print(f"\n[{step_name}] step completed successfully in {duration:.2f} seconds.")
    return True, duration  # <-- 修改：返回成功状态和时长

def main():
    # --- 1. 参数定义 (无修改) ---
    parser = argparse.ArgumentParser(
        description="A simplified wrapper to run LogReducer with just a log name.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "log_name",
        help="The name of the log to process (e.g., 'Apache', 'HDFS', 'Spark')."
    )
    args = parser.parse_args()
    log_name = args.log_name

    # --- 2. 路径处理 (无修改) ---
    input_file = f"../../Logs/{log_name}/{log_name}.log"
    template_dir = f"./LogReducer_result/{log_name}/template/"
    output_dir = f"./LogReducer_result/{log_name}/out/"
    
    print(f"Ensuring output directories exist for '{log_name}'...")
    os.makedirs(template_dir, exist_ok=True)
    os.makedirs(output_dir, exist_ok=True)
    print(f" -> Template dir: {os.path.abspath(template_dir)}")
    print(f" -> Output dir:   {os.path.abspath(output_dir)}")

    logreducer_dir = "./" 
    if not os.path.isdir(logreducer_dir):
        print(f"Error: Directory '{logreducer_dir}' not found.", file=sys.stderr)
        sys.exit(1)

    input_file_abs = os.path.abspath(input_file)
    template_dir_abs = os.path.abspath(template_dir)
    output_dir_abs = os.path.abspath(output_dir)

    if not os.path.exists(input_file_abs):
        print(f"Error: Input file not found at '{input_file_abs}'", file=sys.stderr)
        sys.exit(1)

    try:
        original_size_bytes = os.path.getsize(input_file_abs)
    except OSError as e:
        print(f"Error: Cannot get size of input file '{input_file_abs}'. Reason: {e}", file=sys.stderr)
        sys.exit(1)

    total_start_time = time.time()
    
    # ##### 修改部分 2: 捕获每个步骤的执行时间 #####
    training_duration_sec = 0.0
    compression_duration_sec = 0.0

    # --- 3. 步骤 1: 训练 (Training) ---
    print("\n" + "="*20 + " STEP 1: TRAINING " + "="*20)
    training_command = ["python3", "training.py", "-I", input_file_abs, "-T", template_dir_abs]
    
    # <-- 修改：捕获返回的状态和时长
    success, training_duration_sec = run_command(training_command, "Training", working_dir=logreducer_dir)
    if not success:
        sys.exit(1)

    # --- 4. 步骤 2: 压缩 (Compression) ---
    print("\n" + "="*20 + " STEP 2: COMPRESSION " + "="*20)
    compression_command = [
        "python3", "LogReducer.py",
        "-I", input_file_abs, "-T", template_dir_abs, "-O", output_dir_abs,
        "-TN", "16", "-TL", "0", "-P", "L", "-D", "D", "-E", "Z", "-FN", "0", "-B", "100000", "-m", "Tot"
    ]
    # <-- 修改：捕获返回的状态和时长
    success, compression_duration_sec = run_command(compression_command, "Compression", working_dir=logreducer_dir)
    if not success:
        sys.exit(1)

    total_end_time = time.time()
    total_duration_sec = total_end_time - total_start_time

    # =============================================================
    # --- 5. 计算、打印并保存结果 (已修改) ---
    # =============================================================
    print("\n" + "="*20 + " FINAL RESULTS " + "="*20)
    
    # --- 计算指标 (无修改) ---
    compressed_size_bytes = get_dir_size(output_dir_abs)
    original_size_mb = original_size_bytes / (1024 * 1024)
    compressed_size_mb = compressed_size_bytes / (1024 * 1024)
    compression_ratio = original_size_bytes / compressed_size_bytes if compressed_size_bytes > 0 else 0
    
    # 使用总时长计算总体压缩速度
    compression_speed_mb_s = original_size_mb / total_duration_sec if total_duration_sec > 0 else 0

    # ##### 修改部分 3: 更新打印输出和 JSON 数据 #####
    
    # --- 打印到控制台 (更新) ---
    print("All steps completed successfully!")
    print("-" * 52)
    print(f" -> Training time:     {training_duration_sec:.3f} seconds")
    print(f" -> Compression time:  {compression_duration_sec:.3f} seconds")
    print(f"Total time taken:      {total_duration_sec:.3f} seconds.")
    print("-" * 52)
    print(f"Original Size:     {original_size_bytes} bytes ({original_size_mb:.3f} MB)")
    print(f"Compressed Size:   {compressed_size_bytes} bytes ({compressed_size_mb:.3f} MB)")
    print("-" * 52)
    print(f"Compression Ratio: {compression_ratio:.3f} : 1")
    print(f"Compression Speed: {compression_speed_mb_s:.3f} MB/s (Overall)")
    print("-" * 52)

    # --- 将结果保存到 JSON 文件 (更新) ---
    results_data = {
        "dataset": log_name,
        "training_time_seconds": round(training_duration_sec, 3), # <-- 新增
        "compression_time_seconds": round(compression_duration_sec, 3), # <-- 新增 (推荐)
        "total_time_seconds": round(total_duration_sec, 3),
        "original_size_bytes": original_size_bytes,
        "original_size_mb": round(original_size_mb, 3),
        "compressed_size_bytes": compressed_size_bytes,
        "compressed_size_mb": round(compressed_size_mb, 3),
        "compression_ratio": round(compression_ratio, 3),
        "compression_speed_mb_s": round(compression_speed_mb_s, 3),
        "template_path_abs": template_dir_abs,
        "output_path_abs": output_dir_abs
    }
    
    results_file_path = os.path.join(output_dir_abs, "compression_results.json")
    
    try:
        with open(results_file_path, 'w', encoding='utf-8') as f:
            json.dump(results_data, f, indent=4)
        print(f"Detailed results saved to: '{results_file_path}'")
    except IOError as e:
        print(f"\n[ERROR] Could not write results to file '{results_file_path}'. Reason: {e}", file=sys.stderr)
    
    print("="*52)

if __name__ == "__main__":
    main()