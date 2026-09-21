#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
不依赖 make 的一键测试脚本（Windows 上尤其方便）。

用法：
    python tests/run_all.py                # 自动找 g++ / clang++
    set CXX=clang++ & python tests/run_all.py
    set CXX=python -m ziglang c++ & python tests/run_all.py

它会：
  1. 编译 ad-planner、ip_solver、test_core
  2. 依次运行 C++ 核心测试、Python 解析测试、已知答案测试、集成测试
  3. 汇总结果

如果找不到 C++ 编译器，会跳过需要编译的部分并明确提示。
"""

import os
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "src")
EXE = ".exe" if os.name == "nt" else ""

AGENT = os.path.join(ROOT, "ad-planner" + EXE)
LEGACY = os.path.join(ROOT, "ip_solver" + EXE)
CORE = os.path.join(ROOT, "test_core" + EXE)

COMMON_FLAGS = ["-std=c++17", "-Wall", "-O2",
                "-finput-charset=UTF-8", "-fexec-charset=UTF-8"]

AGENT_SOURCES = [
    "main.cpp", "llm_client.cpp", "model_parser.cpp",
    "solver_call.cpp", "verifier.cpp", "learning_log.cpp", "linear_expr.cpp",
]


def find_compiler():
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"])
    candidates += ["g++", "clang++", "c++"]

    for cand in candidates:
        parts = shlex.split(cand)
        if not parts:
            continue
        if shutil.which(parts[0]) is None:
            continue
        probe = subprocess.run(parts + ["--version"], capture_output=True, text=True)
        if probe.returncode == 0:
            return parts
    return None


def build(compiler, label, sources, output, extra=None):
    cmd = compiler + COMMON_FLAGS + ["-I" + SRC] + sources + ["-o", output]
    if extra:
        cmd += extra
    print("  编译 %-14s %s" % (label, " ".join(os.path.basename(s) for s in sources)))
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True)
    if proc.returncode != 0:
        print("  [FAIL] 编译失败:")
        sys.stdout.write(proc.stderr.decode("utf-8", "replace"))
        return False
    return True


def run(title, cmd, cwd=ROOT):
    print("")
    print("=" * 72)
    print("  " + title)
    print("=" * 72)
    proc = subprocess.run(cmd, cwd=cwd)
    return proc.returncode == 0


def main():
    print("=" * 72)
    print("  ad-planner 一键测试（不依赖 make）")
    print("=" * 72)

    compiler = find_compiler()
    results = []

    if compiler is None:
        print("")
        print("找不到 C++ 编译器（g++ / clang++）。将只运行 Python 部分测试。")
        print("如需完整测试，请安装编译器或设置 CXX 环境变量。")
    else:
        print("")
        print("编译器: %s" % " ".join(compiler))
        print("")
        ok = True
        ok &= build(compiler, "ad-planner",
                    [os.path.join(SRC, s) for s in AGENT_SOURCES], AGENT)
        ok &= build(compiler, "ip_solver", [os.path.join(SRC, "ip_solver.cpp")],
                    LEGACY)
        ok &= build(compiler, "test_core",
                    [os.path.join(HERE, "test_linear_expr.cpp"),
                     os.path.join(SRC, "linear_expr.cpp"),
                     os.path.join(SRC, "verifier.cpp")], CORE)
        if not ok:
            print("")
            print("编译未全部成功，后续需要编译产物的测试会被跳过。")
        results.append(("编译", ok))

    if os.path.exists(CORE):
        results.append(("C++ 核心测试", run("C++ 核心测试（解析 + 验证）", [CORE])))
    else:
        print("\n[跳过] C++ 核心测试（未编译出 test_core）")

    results.append(("Python 解析测试",
                    run("Python 表达式解析测试",
                        [sys.executable, os.path.join(HERE, "test_parser.py")])))
    results.append(("Python 已知答案测试",
                    run("Python 端到端已知答案测试",
                        [sys.executable, os.path.join(HERE, "test_known_cases.py")])))

    if os.path.exists(AGENT):
        results.append(("集成测试",
                        run("端到端集成测试（LLM 调用替换为桩）",
                            [sys.executable,
                             os.path.join(HERE, "test_agent_pipeline.py")])))
    else:
        print("\n[跳过] 端到端集成测试（未编译出 ad-planner）")

    print("")
    print("=" * 72)
    print("  汇总")
    print("=" * 72)
    failed = 0
    for name, ok in results:
        print("  %-20s %s" % (name, "通过" if ok else "失败"))
        if not ok:
            failed += 1

    print("")
    if failed:
        print("  有 %d 项未通过。" % failed)
        return 1
    print("  全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
