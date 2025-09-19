# run_all.py

import subprocess
import os
import sys

def run_command(directory, command):
    """
    在指定的目录中执行一个命令，并等待其完成。

    :param directory: 命令执行的工作目录
    :param command:  要执行的命令列表 (e.g., ["python3", "script.py"])
    :return: True 表示成功, False 表示失败
    """
    # 检查目录是否存在
    if not os.path.isdir(directory):
        print(f"错误: 目录 '{directory}' 不存在。")
        return False
    
    script_path = os.path.join(directory, command[-1])
    if not os.path.isfile(script_path):
        print(f"错误: 脚本 '{script_path}' 不存在。")
        return False

    print(f"\n{'='*20} 开始执行 {' '.join(command)} {'='*20}")
    print(f"工作目录: {os.path.abspath(directory)}")
    
    try:
        # 使用 subprocess.run 来执行命令
        # cwd: 指定命令运行的当前工作目录
        # check=True: 如果命令返回非零退出码（表示有错误），则会引发一个 CalledProcessError 异常
        # text=True: (可选) 让 stdout 和 stderr 以文本形式捕获，而不是字节
        # capture_output=True: (可选) 如果你想捕获输出而不是直接显示在终端，可以加上这个
        result = subprocess.run(
            command, 
            cwd=directory, 
            check=True
        )
        print(f"✅ 命令成功执行完毕。")
        return True
    except FileNotFoundError:
        print(f"❌ 错误: 命令 '{command[0]}' 未找到。请确保 'python3' 已安装并在您的系统路径中。")
        return False
    except subprocess.CalledProcessError as e:
        print(f"❌ 错误: 脚本在执行期间失败，退出码为 {e.returncode}。")
        # 脚本的错误输出会直接显示在终端上，所以这里不需要额外打印
        return False
    except Exception as e:
        print(f"❌ 发生未知错误: {e}")
        return False

def main():
    """
    主函数，按顺序执行所有任务。
    """
    print("开始串行执行所有脚本...")

    # 任务1: 运行 LogShrink
    success = run_command("LogShrink", ["python3", "LogShrink_benchmark.py"])
    if not success:
        print("\n由于 LogShrink 脚本执行失败，程序终止。")
        sys.exit(1)  # 以错误状态退出

    # 任务2: 运行 LogReducer
    success = run_command("LogReducer", ["python3", "benchmark_logreducer.py"])
    if not success:
        print("\n由于 LogReducer 脚本执行失败，程序终止。")
        sys.exit(1)  # 以错误状态退出

    print(f"\n{'='*50}\n🎉 所有脚本均已成功执行完毕！\n{'='*50}")

if __name__ == "__main__":
    main()