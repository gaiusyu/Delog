import os
import sys
import subprocess
import shutil
import time

# ==============================================================================
# ==                      请在这里配置您要处理的数据集列表                      ==
# ==============================================================================
datasets_to_run = [


    'bytedance_C'
]
# ==============================================================================


# --- 脚本和结果文件定义 ---
COMPRESSION_SCRIPT = 'run_logreducer.py'
DECOMPRESSION_SCRIPT = 'LogRestore.py'
COMPRESSION_RESULTS_JSON = 'compression_results.json'
DECOMPRESSION_RESULTS_JSON = 'decompression_results.json'
FINAL_RESULTS_DIR = 'final_results'


def execute_step(command, step_name, dataset_name):
    """
    执行一个子进程命令，并实时打印其输出。
    """
    print("\n" + "="*20 + f" EXECUTING: {step_name} for '{dataset_name}' " + "="*20)
    print(f"Command: {' '.join(command)}")
    print("-" * (42 + len(step_name) + len(dataset_name)))
    
    start_time = time.time()
    
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding='utf-8',
        errors='ignore'
    )
    
    for line in iter(process.stdout.readline, ''):
        sys.stdout.write(line)
        sys.stdout.flush()

    process.wait()
    return_code = process.returncode
    duration = time.time() - start_time

    print("-" * (42 + len(step_name) + len(dataset_name)))
    if return_code != 0:
        print(f"\n[ERROR] {step_name} for '{dataset_name}' FAILED with exit code {return_code} after {duration:.2f}s.", file=sys.stderr)
        return False
    else:
        print(f"[SUCCESS] {step_name} for '{dataset_name}' completed in {duration:.2f}s.")
        return True


def selective_cleanup(dataset_name):
    """
    保留结果JSON文件，然后删除该数据集的所有其他过程文件。
    """
    print("\n" + "-"*20 + f" SELECTIVE CLEANUP for '{dataset_name}' " + "-"*20)
    
    base_dir = f'./LogReducer_result/{dataset_name}'
    
    # 1. 定义要保留的文件的源路径
    comp_json_src = os.path.join(base_dir, 'out', COMPRESSION_RESULTS_JSON)
    decomp_json_src = os.path.join(base_dir, DECOMPRESSION_RESULTS_JSON)
    
    # 2. 创建最终结果目录（如果不存在）
    os.makedirs(FINAL_RESULTS_DIR, exist_ok=True)
    
    # 3. 移动并重命名结果文件
    # 移动压缩结果
    if os.path.exists(comp_json_src):
        comp_json_dest = os.path.join(FINAL_RESULTS_DIR, f'compression_{dataset_name}.json')
        print(f"Preserving: {comp_json_src} -> {comp_json_dest}")
        shutil.move(comp_json_src, comp_json_dest)
    else:
        print(f"Warning: Compression result file not found at {comp_json_src}")

    # 移动解压结果
    if os.path.exists(decomp_json_src):
        decomp_json_dest = os.path.join(FINAL_RESULTS_DIR, f'decompression_{dataset_name}.json')
        print(f"Preserving: {decomp_json_src} -> {decomp_json_dest}")
        shutil.move(decomp_json_src, decomp_json_dest)
    else:
        print(f"Warning: Decompression result file not found at {decomp_json_src}")

    # 4. 删除整个数据集的目录
    if os.path.isdir(base_dir):
        try:
            print(f"Deleting process data directory: {os.path.abspath(base_dir)}")
            shutil.rmtree(base_dir)
            print("Cleanup successful.")
        except Exception as e:
            print(f"[ERROR] Failed to delete directory {base_dir}. Reason: {e}", file=sys.stderr)
    else:
        print(f"Directory not found, skipping cleanup: {base_dir}")


def main():
    # 检查依赖脚本
    for script in [COMPRESSION_SCRIPT, DECOMPRESSION_SCRIPT]:
        if not os.path.exists(script):
            print(f"[FATAL ERROR] Required script '{script}' not found.", file=sys.stderr)
            sys.exit(1)

    total_datasets = len(datasets_to_run)
    print(f"Starting pipeline for {total_datasets} dataset(s): {datasets_to_run}")

    for i, dataset_name in enumerate(datasets_to_run):
        print("\n" + "#"*80)
        print(f"#  PROCESSING DATASET: {dataset_name.upper()} ({i + 1}/{total_datasets})")
        print("#"*80)
        
        # --- 步骤 1: 压缩 ---
        compress_command = ['python3', COMPRESSION_SCRIPT, dataset_name]
        if not execute_step(compress_command, "Compression", dataset_name):
            print(f"Skipping rest of the pipeline for '{dataset_name}' due to compression failure.")
            selective_cleanup(dataset_name) # 即使失败也尝试清理
            continue

        # --- 步骤 2: 解压 ---
        decompress_command = ['python3', DECOMPRESSION_SCRIPT, dataset_name]
        # execute_step(decompress_command, "Decompression", dataset_name)
        
        # --- 步骤 3: 选择性清理 ---
        selective_cleanup(dataset_name)

    print("\n" + "#"*80)
    print("#  PIPELINE COMPLETED FOR ALL DATASETS  #")
    print(f"#  All results have been saved to the '{os.path.abspath(FINAL_RESULTS_DIR)}' directory.  #")
    print("#"*80)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nPipeline interrupted by user. Exiting.")
        sys.exit(1)