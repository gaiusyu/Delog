# -*- coding: utf-8 -*-
import os
import sys
import subprocess
import shutil
import time

# ==============================================================================
# ==                  Configure your experiment parameters here               ==
# ==============================================================================
datasets_to_run = [
    'HealthApp',
]
# Use a parameter combination that you have verified to be successful
LOGSHRINK_PARAMS = { 'E': 'E', 'C': True, 'K': 'lzma', 'V': True, 'P': True, 'S': True, 'wh': 50, 'th': 10, 'NC': 16, 'TN': 4 }
DECOMPRESSION_KERNEL = 'lzma'
DECOMPRESSION_ENCODE = 'E'
LOGS_BASE_DIR = '../../Logs'
# ==============================================================================

# --- (Constant definitions remain unchanged) ---
COMPRESSION_WRAPPER = 'run_LogShrink.py'
DECOMPRESSION_WRAPPER = 'de_logShrink.py'
FINAL_RESULTS_DIR = 'final_results_logshrink'
COMPRESSION_BASE_OUT = './python_compression/out/out'
TEMPLATE_BASE_OUT = './python_compression/template'
DECOMPRESSION_BASE_OUT = './restore_results'

# --- (execute_step and selective_cleanup functions remain unchanged) ---
def execute_step(command, step_name, dataset_name):
    # (This function does not need modification)
    print("\n" + "="*20 + " EXECUTING: {step} for '{dataset}' ".format(step=step_name, dataset=dataset_name) + "="*20)
    print("Command: {}".format(' '.join(command)))
    print("-" * (42 + len(step_name) + len(dataset_name)))
    start_time = time.time()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in iter(process.stdout.readline, b''):
        decoded_line = line.decode('utf-8', 'ignore')
        sys.stdout.write(decoded_line)
        sys.stdout.flush()
    process.wait()
    return_code = process.returncode
    duration = time.time() - start_time
    print("-" * (42 + len(step_name) + len(dataset_name)))
    if return_code != 0:
        sys.stderr.write("\n[ERROR] {step} for '{dataset}' FAILED with exit code {code} after {dur:.2f}s.\n".format(
            step=step_name, dataset=dataset_name, code=return_code, dur=duration))
        return False
    else:
        print("[SUCCESS] {step} for '{dataset}' completed in {dur:.2f}s.".format(
            step=step_name, dataset=dataset_name, dur=duration))
        return True

def selective_cleanup(dataset_name):
    # (This function does not need modification)
    print("\n" + "-"*20 + " SELECTIVE CLEANUP for '{}' ".format(dataset_name) + "-"*20)
    if not os.path.exists(FINAL_RESULTS_DIR):
        os.makedirs(FINAL_RESULTS_DIR)
    comp_json_src = os.path.join(COMPRESSION_BASE_OUT, dataset_name, 'compression_results.json')
    decomp_json_src = os.path.join(DECOMPRESSION_BASE_OUT, 'decompression_{}_results.json'.format(dataset_name))
    kernel_name = LOGSHRINK_PARAMS.get('K', 'unknown')
    if os.path.exists(comp_json_src):
        comp_json_dest = os.path.join(FINAL_RESULTS_DIR, 'logshrink_compression_{}_{}.json'.format(dataset_name, kernel_name))
        print("Preserving: {} -> {}".format(comp_json_src, comp_json_dest))
        shutil.move(comp_json_src, comp_json_dest)
    else:
        print("Warning: Compression result file not found at {}".format(comp_json_src))
    if os.path.exists(decomp_json_src):
        decomp_json_dest = os.path.join(FINAL_RESULTS_DIR, 'logshrink_decompression_{}_{}.json'.format(dataset_name, kernel_name))
        print("Preserving: {} -> {}".format(decomp_json_src, decomp_json_dest)) # Typo fixed
        shutil.move(decomp_json_src, decomp_json_dest)
    else:
        print("Warning: Decompression result file not found at {}".format(decomp_json_src))
    dirs_to_delete = [
        os.path.join(COMPRESSION_BASE_OUT, dataset_name),
        os.path.join(TEMPLATE_BASE_OUT, dataset_name),
        os.path.join(DECOMPRESSION_BASE_OUT, '{}_restored.log'.format(dataset_name)),
        os.path.join(DECOMPRESSION_BASE_OUT, 'decompression_{}_results.json_Log.txt'.format(dataset_name))]
    for path in dirs_to_delete:
        try:
            if os.path.isdir(path):
                print("Deleting directory: {}".format(path))
                shutil.rmtree(path)
            elif os.path.isfile(path):
                print("Deleting file: {}".format(path))
                os.remove(path)
        except Exception as e:
            sys.stderr.write("[ERROR] Failed to delete {}. Reason: {}\n".format(path, e))
    print("Cleanup successful.")

def main():
    for script in [COMPRESSION_WRAPPER, DECOMPRESSION_WRAPPER]:
        if not os.path.exists(script):
            sys.stderr.write("[FATAL ERROR] Required script '{}' not found.\n".format(script))
            sys.exit(1)
            
    total_datasets = len(datasets_to_run)
    print("Starting LogShrink pipeline for {} dataset(s): {}".format(total_datasets, datasets_to_run))
    
    for i, dataset_name in enumerate(datasets_to_run):
        print("\n" + "#"*80)
        print("#  PROCESSING DATASET: {} ({}/{})".format(dataset_name.upper(), i + 1, total_datasets))
        print("#"*80)
        
        # --- Key Fix: Add the -outdir parameter here ---
        compress_command = [
            'python3', COMPRESSION_WRAPPER, 
            '-ds', dataset_name, 
            '-I', LOGS_BASE_DIR,
            '-outdir', COMPRESSION_BASE_OUT # <-- Explicitly tell the compression script where to output!
        ]
        # ----------------------------------------

        for key, value in LOGSHRINK_PARAMS.items():
            if isinstance(value, bool) and value:
                compress_command.append('-{}'.format(key))
            else:
                compress_command.extend(['-{}'.format(key), str(value)])
        
        if not execute_step(compress_command, "Compression", dataset_name):
            print("Skipping rest of the pipeline for '{}' due to compression failure.".format(dataset_name))
            selective_cleanup(dataset_name)
            continue

        decompress_command = [
            'python3', DECOMPRESSION_WRAPPER,
            '-ds', dataset_name,
            '-K', DECOMPRESSION_KERNEL,
            '-E', DECOMPRESSION_ENCODE
        ]
        execute_step(decompress_command, "Decompression", dataset_name)
        
        selective_cleanup(dataset_name)

    print("\n" + "#"*80)
    print("#  PIPELINE COMPLETED FOR ALL DATASETS  #")
    print("#  All results have been saved to the '{}' directory.  #".format(os.path.abspath(FINAL_RESULTS_DIR)))
    print("#"*80)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nPipeline interrupted by user. Exiting.")
        sys.exit(1)