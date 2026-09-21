#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
只在集成测试中使用的 LLM 桩（stub）。

⚠️ 这不是产品代码，也不调用任何真实大模型。
tests/test_agent_pipeline.py 会在测试期间把它临时复制成 solver/llm_call.py，
从而在不联网、不需要 API Key 的情况下，完整跑通 C++ 侧的真实链路：

    main.cpp → llm_client.cpp → model_parser.cpp → solver_call.cpp
             → solve.py → verifier.cpp

它唯一的职责是：冒充 LLM API，返回一个**预先写死**的、格式与真实接口一致
的响应（含 ```json 代码块与 \u003c 转义）。

因此：本测试证明的是"C++ 控制层这条链路是通的"，
**不能**用来证明"程序能自动解出这些题"或任何建模准确率。

选哪一组模型由环境变量 STUB_SCENARIO 决定（由 test_agent_pipeline.py 设置），
不靠问题文本里的关键词来猜——否则题面会被迫写成含暗号的假题目，
反而容易让人误以为程序是从残缺题面里"推导"出了约束。
"""

import base64
import json
import os
import sys

# ---------------------------------------------------------------------------
# 与题目原文一一对应的固定模型。
# 题目原文见 tests/test_agent_pipeline.py 顶部的 PROBLEM_* 常量，
# 每个模型都必须忠实于对应题面，不得凭空多出题面里没有的约束。
# ---------------------------------------------------------------------------

# 工厂题：max 3x + 5y, s.t. 2x + y <= 12, 3x + y <= 18  → 最优 60
FACTORY_MODEL = {
    "v": ["x:甲产品数量", "y:乙产品数量"],
    "c": ["2*x + 1*y <= 12", "3*x + 1*y <= 18"],
    "o": "3*x + 5*y",
    "t": "maximize",
}

# 投资题：题面已给出 2/5/3 万、8%/12%/9%、500 万、150 项、B≥C
# 目标函数故意保留 LLM 原样输出的混合系数写法（历史日志里的那一版），
# 用于回归：修复前同样的字符串被误解析成 0.08
INVEST_MODEL = {
    "v": ["a:A类投资项目数量", "b:B类投资项目数量", "c:C类投资项目数量"],
    "c": ["2*a + 5*b + 3*c <= 500", "a + b + c <= 150", "b >= c"],
    "o": "0.08*2*a + 0.12*5*b + 0.09*3*c",
    "t": "maximize",
}

# 车间题：max x, s.t. x <= 5  → 最优 5
CONFLICT_MODEL = {
    "v": ["x:产量"],
    "c": ["1*x <= 5"],
    "o": "1*x",
    "t": "maximize",
}

# 人为构造的错误模型：引用未声明的变量 z，触发"求解层报错 → 反馈重试"
FACTORY_UNDECLARED_MODEL = {
    "v": ["x:甲产品数量", "y:乙产品数量"],
    "c": ["2*z + 1*y <= 12", "3*x + 1*y <= 18"],
    "o": "3*x + 5*y",
    "t": "maximize",
}

# 人为构造的错误模型：约束互相矛盾，触发"模型无解 → 反馈重试"
INFEASIBLE_MODEL = {
    "v": ["x:产量"],
    "c": ["1*x >= 10", "1*x <= 5"],
    "o": "1*x",
    "t": "maximize",
}

# 场景 -> {首次返回, 重试时返回}
SCENARIO_MODELS = {
    "factory":          {"first": FACTORY_MODEL},
    "invest":           {"first": INVEST_MODEL},
    "retry_undeclared": {"first": FACTORY_UNDECLARED_MODEL, "correct": FACTORY_MODEL},
    "retry_infeasible": {"first": INFEASIBLE_MODEL,         "correct": CONFLICT_MODEL},
}

# 与 src/main.cpp 里回填的重试提示保持一致的判定串
FEEDBACK_MARKER = "上一次建模或求解出错了"


def build_response(content):
    return {
        "id": "stub-0000000000000000000000000000",
        "object": "chat.completion",
        "created": 0,
        "model": "deepseek-ai/DeepSeek-V3",
        "choices": [
            {
                "index": 0,
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop",
            }
        ],
        "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
    }


def main():
    if len(sys.argv) < 4:
        print(json.dumps({"error": "stub 参数不足"}, ensure_ascii=False))
        return 1

    raw = b""
    try:
        raw = base64.b64decode(sys.argv[3])
    except Exception:
        raw = b""

    user_message = ""
    for enc in ("utf-8", "gbk", "cp936"):
        try:
            user_message = raw.decode(enc)
            break
        except (UnicodeDecodeError, LookupError):
            continue

    scenario = os.environ.get("STUB_SCENARIO", "factory")
    if scenario not in SCENARIO_MODELS:
        # 提示写 stderr，stdout 必须只留给 JSON（C++ 侧要解析它）
        sys.stderr.write("stub: 未知 STUB_SCENARIO=%r，回退到 factory\n" % scenario)
        scenario = "factory"

    entry = SCENARIO_MODELS[scenario]
    is_retry = FEEDBACK_MARKER in user_message
    model = entry.get("correct", entry["first"]) if is_retry else entry["first"]

    # 模仿真实模型输出：```json 代码块 + 把 < > 转义成 \u003c \u003e
    body = json.dumps(model, ensure_ascii=False, separators=(",", ":"))
    body = body.replace("<", "\\u003c").replace(">", "\\u003e")
    content = "```json\n" + body + "\n```"

    print(json.dumps(build_response(content), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
