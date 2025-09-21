# DeLog: An Efficient Log Compression Framework with Pattern-based Grouping
### Artifact for USENIX FAST '26 Submission

We sincerely thank the Artifact Evaluation Committee for dedicating their time and expertise to reviewing our work. This artifact provides all the necessary components, data, and instructions to reproduce the key findings presented in our paper. We have strived to make the evaluation process as clear and straightforward as possible and welcome any feedback for improvement.

---

### Table of Contents
1.  [Artifact Overview](#1-artifact-overview)
2.  [Getting Started](#2-getting-started)
    *   [2.1. Hardware Requirements](#21-hardware-requirements)
    *   [2.2. Setup Options](#22-setup-options)
        *   [Option A: Manual Setup (for deep-dive and ablation studies)](#option-a-manual-setup-for-deep-dive-and-ablation-studies)
        *   [Option B: Using Docker (Recommended for quick evaluation)](#option-b-using-docker-recommended-for-quick-evaluation)
3.  [Reproducing Key Evaluation Results](#3-reproducing-key-evaluation-results)
    *   [3.1. Main Evaluation: DeLog Performance (RQ3)](#31-main-evaluation-delog-performance-rq3)
    *   [3.2. Baseline Comparisons (RQ3)](#32-baseline-comparisons-rq3)
    *   [3.3. Ablation Study (RQ5)](#33-ablation-study-rq5)
4.  [Reproducing Specific Claims](#4-reproducing-specific-claims)
    *   [4.1. Claim from Section 2.1: Average Character Length](#41-claim-from-section-21-average-character-length)
    *   [4.2. Claim from Section 2.3: Lossiness in Existing Compressors](#42-claim-from-section-23-lossiness-in-existing-compressors)
    *   [4.3. Claim from Section 2.3: Resource Utilization Overhead](#43-claim-from-section-23-resource-utilization-overhead)
5.  [Supplementary Material](#5-reproducing-supplementary-material-empirical-study-on-log-parsers)
     *   [5.1. The Performance of General-Purpose Compressors on All Logs](#51-the-performance-of-general-compressor-on-all-logs)
     *   [5.2. Run Log Parsers](#52-running-log-parsers)
     *   [5.3. Compression on Parsing Results](#53-compression-based-on-parsing-results)

---

## 1. Artifact Overview

This artifact accompanies the paper "DeLog: An Efficient Log Compression Framework with Pattern-based Grouping". It enables the reproduction of the following key results:

*   **RQ3 & RQ4:** The performance of DeLog on all public benchmark datasets (Loghub and Loghub 2.0).
*   **RQ3 & RQ4:** The performance of baseline methods (LogReducer, Denum, LogShrink) on the same public benchmarks.
*   **RQ5:** The effectiveness of each component of DeLog through a detailed ablation study.
*   **Specific Claims:** Verification of claims made in the paper regarding log characteristics, baseline lossiness, and resource utilization.
*   **Supplementary Study:** An empirical study on the accuracy of existing log parsers and its impact on compression.

> **Note on Private Datasets:** The ByteDance dataset is private due to user privacy and data confidentiality agreements and is **not included** in this artifact. We are fully committed to transparency and have designed the artifact to allow for the complete reproduction of all experiments, figures, tables, and conclusions based on the publicly available **Loghub** and **Loghub 2.0** benchmarks.

## 2. Getting Started

This section outlines the necessary prerequisites and initial setup steps.

### 2.1. Hardware Requirements

- **CPU**: A modern CPU with at least **4 cores** is required. (Recommended: Intel i5, AMD Ryzen 5, or better). DeLog is designed for parallel execution and benefits significantly from multiple cores.
- **Memory (RAM)**:
    - **For DeLog only**: Minimum **4 GB**, Recommended **8 GB**.
    - **For All Baselines**: To reproduce all experiments, a system with **32 GB of RAM** is strongly recommended, as some baselines (e.g., LogReducer) have very high memory consumption.

### 2.2. Setup Options

We offer two methods to set up the environment. For a quick verification of our main results, we highly recommend the Docker-based approach. For detailed ablation studies or modifications to the source code, the manual setup is more suitable.

#### Option A: Manual Setup (for deep-dive and ablation studies)

This traditional approach requires you to manually install dependencies and compile the source code.

**1. Software Dependencies:**
- **Python**: `>= 3.7.3`
- **GCC**: `>= 9.4.0`
- **PCRE2**: `= 10.34`
- **Python Package**: `regex==2012.1.8`

**2. Dataset Preparation:**
- **Loghub (1.0):** Download from [https://github.com/logpai/loghub](https://github.com/logpai/loghub) and place the datasets in `Logs/{logname}/{logname}.log` (e.g., `Logs/HDFS/HDFS.log`).
- **Loghub 2.0:** Download from [https://zenodo.org/records/8275861](https://zenodo.org/records/8275861) and place the datasets in `Loghub_data/{logname}/{logname}.log`.
> **Note:** The `Apache` log is already included in the `Logs/` directory for a quick test run.

**3. Compilation:**
Compile the DeLog compressor and decompressor.
```bash
# Compile Compressor
g++ -std=c++17 -O3 -o Delog_compress compressor.cpp -lpcre2-8 -lstdc++fs -pthread

# Compile Decompressor
g++ -std=c++17 -O2 -o decompress decompressor.cpp -lstdc++fs -pthread -larchive
```

---

#### Option B: Using Docker (Recommended for quick evaluation)

This method uses a pre-configured Docker image that contains all dependencies and the compiled DeLog executable. It is the fastest way to run and verify our compression tool.

**1. Prerequisites:**
- [Docker](https://www.docker.com/products/docker-desktop/) must be installed and running on your system.

**2. Pull the Docker Image:**
Open your terminal and pull the pre-built image from Docker Hub.
```sh
docker pull anonymous4d3a/delog-compressor:latest
```

**3. Prepare Datasets:**
Even with Docker, you need to provide the log files from your local machine.
- Create two directories on your system: `my_logs` for input and `my_output` for results.
- Download the desired log datasets (e.g., HDFS, Apache) and place the log file (e.g., `HDFS.log`) inside the `my_logs` folder.

**4. Run DeLog Compression via Docker:**
Navigate to the directory containing `my_logs` and `my_output`, then execute the command below. The container will automatically process the specified log file and place the compressed archive in your `my_output` folder.

```sh
# On Linux or macOS:
docker run --rm \
-v "$(pwd)/my_logs:/data" \
-v "$(pwd)/my_output:/output" \
anonymous4d3a/delog-compressor \
Apache.log Apache --kernel lzma --threads 4

# On Windows PowerShell:
docker run --rm `
-v "$(pwd)/my_logs:/data" `
-v "$(pwd)/my_output:/output" `
anonymous4d3a/delog-compressor `
Apache.log Apache --kernel lzma --threads 4
```
After the command finishes, the compressed archive (e.g., `chunk_0.tar.xz`) and an experiment log (`experiment_results.csv`) will appear in your `my_output` directory.

**5. Docker Command-Line Options:**
The Docker container accepts the same command-line arguments as the native executable.

- **Usage:** `docker run ... <image_name> [OPTIONS] <input_file> <log_name>`
- **Arguments:**
    - `<input_file>`: The name of the log file inside your `my_logs` folder.
    - `<log_name>`: A logical name for the log type (e.g., `HDFS`, `Apache`). This option enables predefined regular expressions for known benchmark log types to accurately extract timestamps. If you are compressing logs from outside the benchmarks, you can provide any arbitrary string for this parameter. DeLog will still achieve effective performance. Please note that for the compression of all ByteDance logs, we did not use any predefined regular expressions.
- **Options:**
    - `--kernel <name>`: `lzma`, `gzip`, `bzip2`, `lz4`, `none`.
    - `--processing-mode <mode>`: `normal` (DeLog), `fast` (DeLog-L).
    - `--threads <num>`: Number of parallel threads.
    - *(... and others as supported by the executable)*

> **Note:** The automated benchmark scripts (`DeLog_benchmark.py`, etc.) and ablation studies are designed to be run in a manual setup environment (Option A), as they involve file system interactions and code modifications not easily managed within the streamlined Docker workflow.

## 3. Reproducing Key Evaluation Results

This section provides step-by-step instructions to reproduce the main experimental results from our paper. **These instructions assume you have chosen `Option A: Manual Setup`.**

### 3.1. Main Evaluation: DeLog Performance (RQ3)

This script automates the process of running DeLog on all specified datasets and collecting performance metrics.

1.  **Configure Datasets:**
    Open the benchmark script `DeLog_benchmark.py` and add the names of the downloaded datasets you wish to evaluate.
    ```python
    # In DeLog_benchmark.py
    DATASET_THRESHOLDS = {
        'HealthApp': 0,
        'HDFS': 0,
        'Apache': 0,
        'OpenSSH': 0,
        # Add other dataset names from Loghub here
    }
    ```

2.  **Run Benchmark:**
    Execute the Python script. It will run both compression and decompression for DeLog (`normal` mode) and DeLog-L (`fast` mode).
    ```bash
    python3 DeLog_benchmark.py
    ```

3.  **Collect Results:**
    The results will be saved in the root directory in the following CSV files:
    *   `experiments_results.csv`: Compression results for DeLog.
    *   `decompression_results.csv`: Decompression results for DeLog.
    *   `experiments_results_fast.csv`: Compression results for DeLog-L.
    *   `decompression_results_fast.csv`: Decompression results for DeLog-L.

### 3.2. Baseline Comparisons (RQ3)

We provide automated benchmark scripts for all baseline methods.

#### LogShrink
1.  Navigate to the directory: `cd Baselines/LogShrink`
2.  Configure datasets in `LogShrink_benchmark.py`:
    ```python
    datasets_to_run = [ 'HealthApp', 'HDFS', 'Apache' ] # Add desired datasets
    ```
3.  Run the benchmark: `python3 LogShrink_benchmark.py`
4.  Results are saved as JSON files in `Baselines/LogShrink/final_result_logshrink/`.

#### LogReducer
1.  Navigate to the directory: `cd Baselines/LogReducer`
2.  Compile the tool: `make`
3.  Run the benchmark: `python3 benchmark_logreducer.py`
4.  Results are saved as JSON files in `Baselines/LogReducer/final_results/`.

#### Denum
1.  Navigate to the directory: `cd Baselines/Denum`
2.  Run the benchmark: `./benchmark.sh`
3.  Results are saved in `Baselines/Denum/compression_results.csv` and `Baselines/Denum/decompression_results.csv`.

### 3.3. Ablation Study (RQ5)

To reproduce the ablation study, you must manually modify the `compressor.cpp` source file, recompile, and re-run the benchmark for each setting.

**Important:** Remember to revert the changes before proceeding to the next setting.

#### Setting 1: No External Context & No Intrinsic Structure
This setting simplifies all variable tokens to a generic `<*>` tag.

1.  **Modify `compressor.cpp`:**
    In `compressor.cpp`, locate the following block:
    ```cpp
    if (is_variable) {
        std::string compact_id = tag_manager.get_or_create_id(full_tag);
        result_line.append("<").append(compact_id).append(">");
        local_tag_data[full_tag].push_back(std::move(value_to_store));
    }
    ```
    And change it to:
    ```cpp
    if (is_variable) {
        full_tag="<*>"; // Force all variable tags to be generic
        std::string compact_id = tag_manager.get_or_create_id(full_tag);
        result_line.append("<").append(compact_id).append(">");
        local_tag_data[full_tag].push_back(std::move(value_to_store));
    }
    ```
2.  **Recompile and Run:**
    ```bash
    g++ -std=c++17 -O3 -o Delog_compress compressor.cpp -lpcre2-8 -lstdc++fs -pthread
    python3 DeLog_benchmark.py
    ```

#### Setting 2: No External Context
This setting removes the contextual information (preceding token) from the tag generation.

1.  **Modify `compressor.cpp`:**
    Locate the following block:
    ```cpp
    if (type.is_pure_digit || (type.has_digit && !type.has_alpha) || (type.has_digit && type.has_alpha) || type.has_special) {
        is_variable = true;
        if (type.is_pure_digit) {
            if (token.length() <= 2) {
                full_tag = build_structured_tag("", "", std::nullopt, token.length());
            } else {
                full_tag = build_structured_tag(context, generate_regex_like_tag(token), token_index, std::nullopt);
            }
        } else if (type.has_digit && !type.has_alpha) {
            full_tag = build_structured_tag(context, generate_regex_like_tag(token), std::nullopt, std::nullopt);
        } else {
            std::string special_chars_str = extract_special_chars(token);
            full_tag = build_structured_tag(context, "_" + special_chars_str, std::nullopt, std::nullopt);
        }
    ```
    And change all `build_structured_tag(context, ...)` calls to `build_structured_tag("", ...)` to remove the context:
    ```cpp
    if (type.is_pure_digit || (type.has_digit && !type.has_alpha) || (type.has_digit && type.has_alpha) || type.has_special) {
        is_variable = true;
        if (type.is_pure_digit) {
            if (token.length() <= 2) {
                full_tag = build_structured_tag("", "", std::nullopt, token.length());
            } else {
                // Change 'context' to ""
                full_tag = build_structured_tag("", generate_regex_like_tag(token), token_index, std::nullopt);
            }
        } else if (type.has_digit && !type.has_alpha) {
            // Change 'context' to ""
            full_tag = build_structured_tag("", generate_regex_like_tag(token), std::nullopt, std::nullopt);
        } else {
            std::string special_chars_str = extract_special_chars(token);
            // Change 'context' to ""
            full_tag = build_structured_tag("", "_" + special_chars_str, std::nullopt, std::nullopt);
        }
    ```
2.  **Recompile and Run:**
    ```bash
    g++ -std=c++17 -O3 -o Delog_compress compressor.cpp -lpcre2-8 -lstdc++fs -pthread
    python3 DeLog_benchmark.py
    ```

#### Setting 3: Complete DeLog
This is the default configuration. No modifications are needed if you have reverted any previous changes.

## 4. Reproducing Specific Claims

This section details how to verify individual claims made in the paper.

### 4.1. Claim from Section 2.1: Average Character Length

Run the provided Python script to calculate the average length of log lines for a given dataset.

```bash
# Example for a hypothetical dataset
python3 avg_length.py Logs/bytedance_I/bytedance_I.log
```

### 4.2. Claim from Section 2.3: Lossiness in Existing Compressors

This procedure checks for data loss (e.g., dropped leading zeros) in other log compressors.

1.  **Compress and Decompress:** Use a baseline tool (e.g., LogReducer) to compress a log file (like `Apache.log`) and then decompress the resulting archive.
2.  **Compare Files:** Use our `diff_compare.py` script to compare the original log file with the decompressed version. The script is designed to intelligently handle minor, acceptable differences while flagging significant ones.

    ```bash
    # {logname} is the source log name under directory Logs, {differences} is the number of differences will be printed
    python3 diff_compare.py {logname} -m {differences}
    ```

### 4.3. Claim from Section 2.3: Resource Utilization Overhead

We provide scripts to measure peak memory and CPU usage for DeLog and the baselines. These scripts use the `/usr/bin/time -v` command for detailed profiling.

#### For DeLog
```bash
chmod +x performance_benchmark.sh
./performance_benchmark.sh
```
Results are saved to `benchmark_results`. The key metric is **Maximum resident set size (kbytes)**.

#### For Baselines (e.g., LogReducer)
```bash
cd Baselines/LogReducer
chmod +x performance_benchmark.sh
./performance_benchmark.sh
```
Results are saved in `Baselines/LogReducer/benchmark_results`. Similar steps can be followed for other baselines in their respective directories.


#### Table 1: Resource Utilization for Compressing 1GB "bytedance_C" (logC) and "bytedance_D" (logD) of Data.

| Dataset | Tool | Time (s) | CPU Usage (%) | Peak Memory (GB) |
| :--- | :--- | ---: | ---: | ---: |
| LogC (1GB) | DeLog | 18.75 | 269% | 0.74 |
| LogC (1GB) | Denum | 28.94 | 293% | 1.13 |
| LogC (1GB) | LogReducer | 30.82 | 113% | 14.32 |
| LogC (1GB) | LZMA | 32.41 | 313% | 0.55 |
| **LogD (1GB)** | **DeLog** | **32.12** | **266%** | **1.31** |
| LogD (1GB) | Denum | 53.58 | 303% | 1.88 |
| LogD (1GB) | LogReducer | 44.46 | 120% | 22.32 |
| LogD (1GB) | LZMA | 45.21 | 311% | 0.60 |


---
### Additional Note: Manual Execution
For manual compression and decompression of DeLog, please refer to the following commands.

#### Compression
```bash
# Usage: ./Delog_compress {logname} {filetype} {chunksize} {threads} {threshold} {kernel} {mode}
# kernel: lzma, gzip, bzip2
# mode:   normal (DeLog), fast (DeLog-L)
# filetype: text (use this for all benchmarks), json (for debugging)
# threshold: 0 (use this for all benchmarks)
# Example for HDFS log
./Delog_compress HDFS text 100000 4 0 lzma normal
```

#### Decompression
```bash
# Usage: ./decompress {input_path} {output_file} {threads}

# Example for Zookeeper output
./decompress output/Zookeeper decompressed_Zookeeper.log 4
```
> **Important Decompression Note:** The current decompressor is specifically tailored for certain variable types like IP addresses found in the Apache logs. When applying DeLog to new datasets with different structured variables (e.g., MAC addresses, UUIDs), a custom recovery function may need to be implemented, similar to the logic around line 809 in `decompressor.cpp`, to ensure lossless reconstruction.

## 5. Reproducing Supplementary Material: Empirical Study on Log Parsers

This section reproduces the experiment from our supplementary material, which investigates discrepancies in log parser accuracy reported by existing benchmarks.

### 5.1. The Performance of General Compressor on All Logs
1.  **gzip, lzma, bzip2:**
    ```bash
    python general_compressor_benchmark.py
    ```
    Results are saved to `general_chunked_results.csv`



### 5.2. Running Log Parsers

1.  Navigate to the parser's directory:
    ```bash
    cd Log_Parsers/{log_parser_name}
    ```
2.  **Configuration:** Before running, you may need to edit the `benchmark.py` script within each parser's directory to set the correct log file paths and names.
3.  **Run Parsing:**
    ```bash
    python benchmark.py
    ```
    Repeat this for each log parser you wish to evaluate.

### 5.3. Compression Based on Parsing Results

After generating the parsed templates from the previous step, run the following script from the repository's root directory to perform compression based on these templates.

```bash
python run_all.py
```

This script will use the outputs from the log parsers to evaluate a parse-then-compress pipeline, generating the results shown in the supplementary material.