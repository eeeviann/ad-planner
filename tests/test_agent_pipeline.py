#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端集成测试：跑通真实的 C++ 全链路（只把 LLM 网络调用换成桩）。

覆盖场景：
  1. 工厂生产计划 —— 一次成功，验证四步验证报告齐全
  2. 投资规划     —— 目标函数是混合系数写法，断言答案是 60 而不是 0.08
  3. 未声明变量   —— 求解层报错 → 反馈重试 → 第二次成功
  4. 约束矛盾     —— 模型无解 → 反馈重试 → 第二次成功

需要先编译好 ad-planner：
    make
或
    g++ -std=c++17 -Wall -O2 -Isrc -o ad-planner.exe src/*.cpp

运行：
    python tests/test_agent_pipeline.py
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
LLM_SCRIPT = os.path.join(ROOT, "solver", "llm_call.py")
STUB_SCRIPT = os.path.join(HERE, "stub_llm_call.py")
BACKUP = LLM_SCRIPT + ".orig"

SCENARIOS = [
    {
        "name": "场景 1 · 工厂生产计划（一次成功）",
        "problem": "某工厂生产甲、乙两种产品。甲每件利润3元，乙每件利润5元。求最大利润。",
        "must_contain": [
            "=== 解算成功 ===",
            "最优值: 60",
            "[通过] 约束检查通过：2/2 条约束全部满足",
            "[通过] 目标值检查通过：重算 60 与报告值一致",
        ],
        "must_not_contain": ["解验证失败", "所有重试均失败"],
    },
    {
        "name": "场景 2 · 投资规划（回归：不能是 0.08）",
        "problem": "投资问题：有三种投资A、B、C，总资金500万，求最大收益。",
        "must_contain": [
            "=== 解算成功 ===",
            "最优值: 60",
            "[通过] 目标值检查通过：重算 60 与报告值一致",
        ],
        "must_not_contain": ["最优值: 0.08", "解验证失败", "所有重试均失败"],
    },
    {
        "name": "场景 3 · 未声明变量 → 反馈重试后成功",
        "problem": "重试演示：某工厂生产甲、乙两种产品，请建模并求解。",
        "must_contain": [
            "求解层返回 ERROR",
            "未在 variables 中声明",
            "[尝试 2/3]",
            "=== 解算成功 ===",
            "[通过] 约束检查通过：2/2 条约束全部满足",
        ],
        "must_not_contain": ["所有重试均失败"],
    },
    {
        "name": "场景 4 · 约束矛盾 → 反馈重试后成功",
        "problem": "无解演示：某工厂产量问题，约束条件有些冲突，请建模求解。",
        "must_contain": [
            "INFEASIBLE",
            "[尝试 2/3]",
            "=== 解算成功 ===",
            "最优值: 5",
        ],
        "must_not_contain": ["所有重试均失败"],
    },
]


def find_binary():
    for name in ("ad-planner.exe", "ad-planner"):
        path = os.path.join(ROOT, name)
        if os.path.exists(path):
            return path
    return None


def decode(raw):
    """C++ 输出的编码取决于编译器与终端，这里做一次宽松解码。"""
    if raw is None:
        return ""
    for enc in ("utf-8", "gbk", "cp936", "utf-16"):
        try:
            return raw.decode(enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return raw.decode("latin-1", errors="replace")


def main():
    binary = find_binary()
    if binary is None:
        print("找不到 ad-planner 可执行文件，请先编译：")
        print("    make")
        print("或  g++ -std=c++17 -Wall -O2 -Isrc -o ad-planner.exe src/*.cpp")
        return 2

    print("=" * 76)
    print("  端到端集成测试（C++ 全链路，LLM 调用替换为桩）")
    print("  二进制: %s" % os.path.relpath(binary, ROOT))
    print("=" * 76)

    # C++ 侧用 "python"（PATH 上的默认解释器）调用 Python 脚本。
    # 这里把当前解释器所在目录放到 PATH 最前面，保证子进程用的是
    # 装了 ortools 的那个 python。
    child_env = os.environ.copy()
    py_dir = os.path.dirname(os.path.abspath(sys.executable))
    child_env["PATH"] = py_dir + os.pathsep + child_env.get("PATH", "")
    print("  子进程 PATH 首位: %s" % py_dir)

    # 备份真实的 llm_call.py，测试结束后务必还原
    if not os.path.exists(BACKUP):
        shutil.copy2(LLM_SCRIPT, BACKUP)
    failures = []
    try:
        for sc in SCENARIOS:
            shutil.copy2(STUB_SCRIPT, LLM_SCRIPT)
            print("\n" + "-" * 76)
            print(sc["name"])
            print("  问题: %s" % sc["problem"])

            proc = subprocess.run(
                # 第二个参数是占位 API Key（桩不校验它）
                [binary, sc["problem"], "stub-key-not-used"],
                capture_output=True, cwd=ROOT, timeout=120, env=child_env,
            )
            output = decode(proc.stdout) + decode(proc.stderr)
            print("  进程退出码: %s" % proc.returncode)
            for line in output.splitlines():
                print("    | %s" % line)

            ok = True
            for token in sc["must_contain"]:
                if token not in output:
                    print("  [FAIL] 输出中缺少: %s" % token)
                    ok = False
            for token in sc["must_not_contain"]:
                if token in output:
                    print("  [FAIL] 输出中不应出现: %s" % token)
                    ok = False

            if ok:
                print("  [PASS]")
            else:
                failures.append(sc["name"])
    finally:
        # 还原真实脚本
        if os.path.exists(BACKUP):
            shutil.move(BACKUP, LLM_SCRIPT)
        print("\n[solver/llm_call.py 已还原]")

    print("\n" + "=" * 76)
    if failures:
        print("  失败 %d / %d 个场景" % (len(failures), len(SCENARIOS)))
        for name in failures:
            print("    - %s" % name)
        print("=" * 76)
        return 1
    print("  全部 %d 个场景通过" % len(SCENARIOS))
    print("=" * 76)
    return 0


if __name__ == "__main__":
    sys.exit(main())
