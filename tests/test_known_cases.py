#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端已知答案测试：把模型 JSON 喂给 solver/solve.py，
用「手工推导的期望值」和「独立暴力枚举」双重校验结果。

暴力枚举的参考实现是每个案例单独手写的 lambda，不依赖 solve.py 的解析器，
因此两条路径相互独立。

运行：
    python tests/test_known_cases.py
"""

import itertools
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SOLVE_PY = os.path.join(ROOT, "solver", "solve.py")

TOL = 1e-6

# --------------------------------------------------------------------------
# 案例定义
#   model     : 交给 solve.py 的 JSON
#   expected  : 手工推导的期望最优值与最优解
#   reference : 独立的暴力枚举参考实现 (objective, constraints, bounds)
# --------------------------------------------------------------------------
CASES = [
    {
        "name": "案例 1 · 工厂生产计划（两变量最大化）",
        "note": "教材级两变量线性整数规划，手工可解",
        "model": {
            "variables": ["x", "y"],
            "constraints": ["2*x + 1*y <= 12", "3*x + 1*y <= 18"],
            "objective": "3*x + 5*y",
            "type": "maximize",
            "integer_vars": ["x", "y"],
        },
        "expected": {"objective": 60.0, "solution": {"x": 0.0, "y": 12.0}},
        "reference": {
            "objective": lambda v: 3 * v["x"] + 5 * v["y"],
            "constraints": [
                lambda v: 2 * v["x"] + 1 * v["y"] <= 12,
                lambda v: 3 * v["x"] + 1 * v["y"] <= 18,
            ],
            "bounds": {"x": range(0, 13), "y": range(0, 13)},
            "maximize": True,
        },
    },
    {
        "name": "案例 2 · 0/1 背包问题（三件物品最大化）",
        "note": "每个变量有上界 1，验证 xi <= 1 形式的上界约束",
        "model": {
            "variables": ["x1", "x2", "x3"],
            "constraints": ["3*x1 + 2*x2 + 4*x3 <= 8",
                            "x1 <= 1", "x2 <= 1", "x3 <= 1"],
            "objective": "2000*x1 + 3000*x2 + 5000*x3",
            "type": "maximize",
            "integer_vars": ["x1", "x2", "x3"],
        },
        "expected": {"objective": 8000.0,
                     "solution": {"x1": 0.0, "x2": 1.0, "x3": 1.0}},
        "reference": {
            "objective": lambda v: 2000 * v["x1"] + 3000 * v["x2"] + 5000 * v["x3"],
            "constraints": [
                lambda v: 3 * v["x1"] + 2 * v["x2"] + 4 * v["x3"] <= 8,
                lambda v: v["x1"] <= 1,
                lambda v: v["x2"] <= 1,
                lambda v: v["x3"] <= 1,
            ],
            "bounds": {"x1": range(0, 2), "x2": range(0, 2), "x3": range(0, 2)},
            "maximize": True,
        },
    },
    {
        # 这一条就是 learning_log.txt 里算出 0.08 的那道题。
        # objective 用的是当时 LLM 原样输出的字符串，一个字都没改。
        "name": "案例 3 · 投资规划（回归：曾经错算成 0.08 的那道题）",
        "note": "目标函数字符串取自历史运行日志，修复前返回 0.08",
        "model": {
            "variables": ["a", "b", "c"],
            "constraints": ["2*a + 5*b + 3*c <= 500",
                            "a + b + c <= 150",
                            "b >= c"],
            "objective": "0.08*2*a + 0.12*5*b + 0.09*3*c",
            "type": "maximize",
            "integer_vars": ["a", "b", "c"],
        },
        "expected": {"objective": 60.0,
                     "solution": {"a": 0.0, "b": 100.0, "c": 0.0}},
        "reference": {
            # 0.08*2 = 0.16, 0.12*5 = 0.6, 0.09*3 = 0.27
            "objective": lambda v: 0.16 * v["a"] + 0.6 * v["b"] + 0.27 * v["c"],
            "constraints": [
                lambda v: 2 * v["a"] + 5 * v["b"] + 3 * v["c"] <= 500,
                lambda v: v["a"] + v["b"] + v["c"] <= 150,
                lambda v: v["b"] >= v["c"],
            ],
            "bounds": {"a": range(0, 151), "b": range(0, 101), "c": range(0, 151)},
            "maximize": True,
        },
    },
    {
        "name": "案例 4 · 饲料混合（最小化问题）",
        "note": "验证 minimize 方向与 >= 约束",
        "model": {
            "variables": ["x", "y"],
            "constraints": ["3*x + 2*y >= 12", "2*x + 4*y >= 14"],
            "objective": "0.3*x + 0.5*y",
            "type": "minimize",
            "integer_vars": ["x", "y"],
        },
        "expected": {"objective": 1.9, "solution": {"x": 3.0, "y": 2.0}},
        "reference": {
            "objective": lambda v: 0.3 * v["x"] + 0.5 * v["y"],
            "constraints": [
                lambda v: 3 * v["x"] + 2 * v["y"] >= 12,
                lambda v: 2 * v["x"] + 4 * v["y"] >= 14,
            ],
            "bounds": {"x": range(0, 21), "y": range(0, 21)},
            "maximize": False,
        },
    },
    {
        "name": "案例 5 · 整数性关键案例（LP 松弛解不可行）",
        "note": "连续松弛最优约 10.83，整数最优 12，验证整数变量真的被约束为整数",
        "model": {
            "variables": ["x", "y"],
            "constraints": ["6*x + 4*y <= 13"],
            "objective": "5*x + 4*y",
            "type": "maximize",
            "integer_vars": ["x", "y"],
        },
        "expected": {"objective": 12.0, "solution": {"x": 0.0, "y": 3.0}},
        "reference": {
            "objective": lambda v: 5 * v["x"] + 4 * v["y"],
            "constraints": [lambda v: 6 * v["x"] + 4 * v["y"] <= 13],
            "bounds": {"x": range(0, 4), "y": range(0, 5)},
            "maximize": True,
        },
    },
]


def brute_force(ref):
    """独立参考实现：穷举所有整数组合。"""
    names = list(ref["bounds"].keys())
    best_value = None
    best_solution = None
    for combo in itertools.product(*[ref["bounds"][n] for n in names]):
        v = dict(zip(names, combo))
        if not all(c(v) for c in ref["constraints"]):
            continue
        value = ref["objective"](v)
        if best_value is None:
            best_value, best_solution = value, v
        elif ref["maximize"] and value > best_value:
            best_value, best_solution = value, v
        elif not ref["maximize"] and value < best_value:
            best_value, best_solution = value, v
    return best_solution, best_value


def run_solver(model):
    proc = subprocess.run(
        [sys.executable, SOLVE_PY],
        input=json.dumps(model, ensure_ascii=False),
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError("solve.py 退出码 %s, stderr=%s"
                           % (proc.returncode, proc.stderr.strip()))
    return json.loads(proc.stdout)


def main():
    print("=" * 76)
    print("  端到端已知答案测试（求解器 × 手工期望值 × 暴力枚举）")
    print("=" * 76)

    failures = []
    for case in CASES:
        print("\n" + "-" * 76)
        print(case["name"])
        print("  说明: %s" % case["note"])

        result = run_solver(case["model"])
        exp = case["expected"]
        ref_solution, ref_value = brute_force(case["reference"])

        print("  求解器状态: %s" % result["status"])
        print("  规范化目标函数: %s" % result["normalized_model"]["objective"])
        print("  求解器结果: 目标值 = %s, 解 = %s"
              % (result["objective_value"], result["solution"]))
        print("  暴力枚举:   目标值 = %s, 解 = %s" % (ref_value, ref_solution))
        print("  期望值:     目标值 = %s, 解 = %s"
              % (exp["objective"], exp["solution"]))

        ok = True
        if result["status"] != "OPTIMAL":
            print("  [FAIL] 状态不是 OPTIMAL")
            ok = False
        if abs(result["objective_value"] - exp["objective"]) > 1e-6:
            print("  [FAIL] 目标值与期望不符")
            ok = False
        for name, want in exp["solution"].items():
            got = result["solution"].get(name)
            if got is None or abs(got - want) > 1e-6:
                print("  [FAIL] 变量 %s 期望 %s，实际 %s" % (name, want, got))
                ok = False
        if abs(result["objective_value"] - ref_value) > 1e-6:
            print("  [FAIL] 与暴力枚举的目标值不一致")
            ok = False
        for name, want in ref_solution.items():
            got = result["solution"].get(name)
            if got is None or abs(got - float(want)) > 1e-6:
                print("  [FAIL] 与暴力枚举的解不一致：变量 %s" % name)
                ok = False

        if ok:
            print("  [PASS] 三方一致")
        else:
            failures.append(case["name"])

    print("\n" + "=" * 76)
    if failures:
        print("  失败 %d / %d 项" % (len(failures), len(CASES)))
        for name in failures:
            print("    - %s" % name)
        print("=" * 76)
        return 1
    print("  全部 %d 个案例通过" % len(CASES))
    print("=" * 76)
    return 0


if __name__ == "__main__":
    sys.exit(main())
