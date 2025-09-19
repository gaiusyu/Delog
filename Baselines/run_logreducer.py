import sys
import os
import argparse
import subprocess
import time

def get_dir_size(directory):
    # ... (此函数无需修改)
    total_size = 0
    for dirpath, _, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if os.path.isfile(fp):
                total_size += os.path.getsize(fp)
    return total_size

def run_command(command, step_name, working_dir=None): # 新增 working_dir 参数
    """
    执行一个shell命令，并检查其返回状态。

    :param command: 要执行的命令列表。
    :param step_name: 当前步骤的名称（例如 "Training" 或 "Compression"）。
    :param working_dir: 执行命令时的工作目录。
    :return: 成功返回 True，失败返回 False。
    """
    print("-" * 50)
    print(f"[{step_name}] Executing command:")
    print(" ".join(command))
    if working_dir:
        print(f"Working Directory: {working_dir}")
    print("-" * 50)
    
    start_time = time.time()
    
    # 修改：在 subprocess.run 中使用 cwd 参数
    result = subprocess.run(
        command, 
        capture_output=True, 
        text=True, 
        encoding='utf-8', 
        errors='ignore',
        cwd=working_dir  # 设置工作目录
    )
    
    end_time = time.time()
    duration = end_time - start_time
    
    # ... (后续打印和错误检查部分无需修改)
    if result.stdout:
        print(f"[{step_name}] STDOUT:\n{result.stdout.strip()}")
    if result.stderr:
        print(f"[{step_name}] STDERR:\n{result.stderr.strip()}", file=sys.stderr)
    
    if result.returncode != 0:
        print(f"\n[ERROR] {step_name} step failed with exit code {result.returncode}.", file=sys.stderr)
        return False
        
    print(f"\n[{step_name}] step completed successfully in {duration:.2f} seconds.")
    return True

def main():
    # --- 参数定义 ---
    # ... (此部分无需修改)
    parser = argparse.ArgumentParser(
        description="A wrapper script to run LogReducer's training and compression steps sequentially.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--Input", "-I", required=True, help="The input log file path (e.g., /path/xx.log)")
    # ... (其他所有参数定义保持不变)
    parser.add_argument("--Template", "-T", default="./template/", help="The directory to store/use templates.")
    parser.add_argument("--Output", "-O", default="./out/", help="The output directory for compressed files.")
    parser.add_argument("--TemplateLevel", "-TL", default="0", choices=["0", "N"], help="Template level for compression.")
    parser.add_argument("--MatchPolicy", "-P", default="L", choices=["L", "T"], help="Match policy for compression.")
    parser.add_argument("--TimeDiff", "-D", default="D", choices=["D", "ND"], help="Time diff policy for compression.")
    parser.add_argument("--EncoderMode", "-E", default="Z", choices=["Z", "NE", "P"], help="Encoder type for compression.")
    parser.add_argument("--MaxThreadNum", "-TN", default="4", help="Max number of threads for compression.")
    parser.add_argument("--ProcFilesNum", "-FN", default="0", help="Max block num a single thread can process.")
    parser.add_argument("--BlockSize", "-B", default="100000", help="The size of lines in a single block.")
    parser.add_argument("--Mode", "-m", default="Tot", choices=["Tot", "Seg"], help="Mode of compression.")
    
    args = parser.parse_args()

    # --- 路径处理 ---
    # 新增：定义 LogReducer 的根目录
    logreducer_dir = "LogReducer"
    
    # 检查 LogReducer 目录是否存在
    if not os.path.isdir(logreducer_dir):
        print(f"Error: Directory '{logreducer_dir}' not found. Please ensure this script is in the same directory as the 'LogReducer' folder.", file=sys.stderr)
        sys.exit(1)

    # 将相对路径转换为绝对路径，这样在切换工作目录后路径依然有效
    input_file_abs = os.path.abspath(args.Input)
    template_dir_abs = os.path.abspath(args.Template)
    output_dir_abs = os.path.abspath(args.Output)

    # 检查输入文件是否存在
    if not os.path.exists(input_file_abs):
        print(f"Error: Input file not found at '{input_file_abs}'", file=sys.stderr)
        sys.exit(1)
        
    try:
        original_size_bytes = os.path.getsize(input_file_abs)
    except OSError as e:
        print(f"Error: Cannot get size of input file '{input_file_abs}'. Reason: {e}", file=sys.stderr)
        sys.exit(1)

    total_start_time = time.time()

    # --- 步骤 1: 训练 (Training) ---
    print("\n" + "="*20 + " STEP 1: TRAINING " + "="*20)
    training_command = [
        "python3", "training.py", # 命令本身现在是相对于 LogReducer 目录的
        "-I", input_file_abs,
        "-T", template_dir_abs
    ]
    # 修改：传入 working_dir
    if not run_command(training_command, "Training", working_dir=logreducer_dir):
        sys.exit(1)

    # --- 步骤 2: 压缩 (Compression) ---
    print("\n" + "="*20 + " STEP 2: COMPRESSION " + "="*20)
    compression_command = [
        "python3", "LogReducer.py", # 命令本身现在是相对于 LogReducer 目录的
        "-I", input_file_abs,
        "-T", template_dir_abs,
        "-O", output_dir_abs,
        "-TL", args.TemplateLevel,
        "-P", args.MatchPolicy,
        "-D", args.TimeDiff,
        "-E", args.EncoderMode,
        "-TN", args.MaxThreadNum,
        "-FN", args.ProcFilesNum,
        "-B", args.BlockSize,
        "-m", args.Mode
    ]
    # 修改：传入 working_dir
    if not run_command(compression_command, "Compression", working_dir=logreducer_dir):
        sys.exit(1)

    total_end_time = time.time()
    total_duration_sec = total_end_time - total_start_time
    
    # --- 计算和打印结果 ---
    # ... (此部分无需修改，因为已经使用了绝对路径 output_dir_abs)
    print("\n" + "="*20 + " FINAL RESULTS " + "="*20)

    compressed_size_bytes = get_dir_size(output_dir_abs)
    original_size_mb = original_size_bytes / (1024 * 1024)
    compressed_size_mb = compressed_size_bytes / (1024 * 1024)
    compression_ratio = original_size_bytes / compressed_size_bytes if compressed_size_bytes > 0 else 0
    compression_speed_mb_s = original_size_mb / total_duration_sec if total_duration_sec > 0 else 0

    print("All steps (Training and Compression) completed successfully!")
    print(f"Total time taken: {total_duration_sec:.3f} seconds.")
    print("-" * 52)
    print(f"Original Size: {original_size_bytes} bytes ({original_size_mb:.3f} MB)")
    print(f"Compressed Size: {compressed_size_bytes} bytes ({compressed_size_mb:.3f} MB)")
    print("-" * 52)
    print(f"Compression Ratio: {compression_ratio:.3f} : 1")
    print(f"Compression Speed: {compression_speed_mb_s:.3f} MB/s")
    print("-" * 52)
    print(f"Templates are in: '{template_dir_abs}'")
    print(f"Compressed output is in: '{output_dir_abs}'")
    print("="*52)

if __name__ == "__main__":
    main()