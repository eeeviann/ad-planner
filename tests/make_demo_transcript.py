#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 examples/demo_transcript.txt：真实跑一遍离线流程并记录输出。"""
import json
import os
import platform
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PY = sys.executable

INVEST = {
    "variables": ["a", "b", "c"],
    "constraints": ["2*a + 5*b + 3*c <= 500", "a + b + c <= 150", "b >= c"],
    "objective": "0.08*2*a + 0.12*5*b + 0.09*3*c",
    "type": "maximize",
    "integer_vars": ["a", "b", "c"],
}
BAD = {
    "variables": ["x"],
    "constraints": ["2*z <= 4"],
    "objective": "x",
    "type": "maximize",
}

lines = []
w = lines.append

w("=" * 72)
w("  ad-planner 离线运行记录（无需 API Key）")
w("  生成时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
try:
    ortools_ver = subprocess.run([PY, "-c", "import ortools;print(ortools.__version__)"],
                                 capture_output=True, text=True).stdout.strip()
except Exception:
    ortools_ver = "?"
w("  Python: %s   ortools: %s   OS: %s"
  % (platform.python_version(), ortools_ver, platform.system()))
w("=" * 72)
w("")
w("  说明：本文件由 tests/make_demo_transcript.py 自动生成，内容为本地实跑输出。")
w("  其中第 5 段用桩替换了 LLM 网络调用（见该段开头的提示），")
w("  其余各段均为真实执行结果。真实大模型调用记录见 examples/real_llm_run_before_fix.txt。")


def scrub(text):
    """把本机绝对路径（含用户名）替换掉，避免把个人目录写进公开仓库。"""
    if not text:
        return text
    home = os.path.expanduser("~")
    for pat in {home, home.replace("\\", "/"), home.replace("/", "\\")}:
        if pat:
            text = text.replace(pat, "<HOME>")
    return text


def run_solver(model):
    proc = subprocess.run([PY, os.path.join("solver", "solve.py")],
                          input=json.dumps(model, ensure_ascii=False),
                          capture_output=True, text=True, cwd=ROOT)
    return scrub(proc.stdout.strip())


def run_cmd(title, args, cwd=ROOT, header=None):
    w("")
    w("$ " + (header or " ".join(args)))
    w("")
    proc = subprocess.run(args, capture_output=True, text=True, cwd=cwd)
    out = scrub((proc.stdout + proc.stderr).rstrip())
    for line in out.splitlines():
        w(line)
    w("")


w("")
w("-" * 72)
w("1. 把模型直接喂给求解层 solver/solve.py")
w("-" * 72)
w("")
w("   输入（objective 用的是历史日志里 LLM 原样输出的字符串）：")
w("")
w(json.dumps(INVEST, ensure_ascii=False, indent=2))
w("")
w("$ echo '<上面的 JSON>' | python solver/solve.py")
w("")
parsed = json.loads(run_solver(INVEST))
w(json.dumps(parsed, ensure_ascii=False, indent=2))
w("")
w("   要点：normalized_model.objective 已正确展开为 0.16*a + 0.6*b + 0.27*c，")
w("   目标值 60.0（修复前同样的输入只返回 0.08，详见 docs/FIXES.md）。")

w("")
w("-" * 72)
w("2. 非法模型会被拒绝，并给出可读原因（供 Agent 反馈给 LLM 重试）")
w("-" * 72)
w("")
w("   输入：约束里引用了未声明的变量 z")
w("")
w(json.dumps(BAD, ensure_ascii=False, indent=2))
w("")
w("$ echo '<上面的 JSON>' | python solver/solve.py")
w("")
w(run_solver(BAD))
w("")
w("   要点：修复前这里会抛 ValueError，traceback 打到 stdout，")
w("   C++ 侧只看到无法解析的内容，日志里记为 '求解失败: ERROR'。")

w("")
w("-" * 72)
w("3. C++ 核心测试（线性表达式解析 + 解验证）")
w("-" * 72)
run_cmd("C++ core test", [os.path.join(ROOT, "test_core.exe")],
        header="./test_core.exe")

w("")
w("-" * 72)
w("4. Python 测试")
w("-" * 72)
run_cmd("parser test", [PY, os.path.join("tests", "test_parser.py")],
        header="python tests/test_parser.py")
run_cmd("known cases", [PY, os.path.join("tests", "test_known_cases.py")],
        header="python tests/test_known_cases.py")

w("")
w("-" * 72)
w("5. 端到端集成测试：真实 C++ 全链路，但 LLM 调用替换为桩（离线，无需 API Key）")
w("-" * 72)
w("")
w("   ⚠️ 这一段**不是**真实大模型的运行结果：")
w("   LLM 的网络调用被 tests/stub_llm_call.py 替换成了预先写死的固定模型，")
w("   用来验证 C++ 控制层（模型解析 → 求解器调用 → 独立验证 → 失败重试）")
w("   这条链路是否跑通。它不代表真实大模型的建模准确率。")
w("")
w("   真实调用大模型（SiliconFlow / DeepSeek-V3）的记录另见")
w("   examples/real_llm_run_before_fix.txt。")
w("")
w("   本段实际启动 ad-planner.exe，跑通")
w("   main.cpp → llm_client.cpp → model_parser.cpp → solver_call.cpp")
w("   → solve.py → verifier.cpp 的完整链路。")
w("")
run_cmd("pipeline", [PY, os.path.join("tests", "test_agent_pipeline.py")],
        header="python tests/test_agent_pipeline.py")

out_path = os.path.join(ROOT, "examples", "demo_transcript.txt")
with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print("wrote %s (%d lines)" % (out_path, len(lines)))
