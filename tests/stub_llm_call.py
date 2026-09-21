#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
只在集成测试中使用的 LLM 桩（stub）。

它不是产品代码。tests/test_agent_pipeline.py 会在测试期间把它临时复制成
solver/llm_call.py，从而在不联网、不需要 API Key 的情况下，
完整跑通 C++ 侧的真实链路：
    main.cpp → llm_client.cpp → model_parser.cpp → solver_call.cpp
             → solve.py → verifier.cpp

它唯一的职责是：冒充 LLM API，按关键词返回一个预先写好的、格式与真实
接口一致的响应（含 ```json 代码块与 \u003c 转义）。
"""

import base64
import json
import sys

# 关键词 -> 首次调用时返回的模型
# 注意顺序：更具体的关键词要放在前面（"重试"/"无解" 的问题文本里也会提到"工厂"）
FIRST_MODEL = {
    # 引用未声明的变量 z，用来触发"求解层报错 → 反馈重试"
    "重试": {
        "v": ["x:甲产品数量", "y:乙产品数量"],
        "c": ["2*z + 1*y <= 12", "3*x + 1*y <= 18"],
        "o": "3*x + 5*y",
        "t": "maximize",
    },
    # 约束互相矛盾，用来触发"模型无解 → 反馈重试"
    "无解": {
        "v": ["x:产量"],
        "c": ["1*x >= 10", "1*x <= 5"],
        "o": "1*x",
        "t": "maximize",
    },
    # 目标函数用 LLM 原样输出的混合系数写法（历史日志里的那一版）
    "投资": {
        "v": ["a:A类投资项目数量", "b:B类投资项目数量", "c:C类投资项目数量"],
        "c": ["2*a + 5*b + 3*c <= 500", "a + b + c <= 150", "b >= c"],
        "o": "0.08*2*a + 0.12*5*b + 0.09*3*c",
        "t": "maximize",
    },
    "工厂": {
        "v": ["x:甲产品数量", "y:乙产品数量"],
        "c": ["2*x + 1*y <= 12", "3*x + 1*y <= 18"],
        "o": "3*x + 5*y",
        "t": "maximize",
    },
}

# 识别到"上一次建模出错了"的反馈后，统一返回的正确模型
CORRECT_MODEL = {
    "工厂": {
        "v": ["x:甲产品数量", "y:乙产品数量"],
        "c": ["2*x + 1*y <= 12", "3*x + 1*y <= 18"],
        "o": "3*x + 5*y",
        "t": "maximize",
    },
    "重试": {
        "v": ["x:甲产品数量", "y:乙产品数量"],
        "c": ["2*x + 1*y <= 12", "3*x + 1*y <= 18"],
        "o": "3*x + 5*y",
        "t": "maximize",
    },
    "无解": {
        "v": ["x:产量"],
        "c": ["1*x <= 5"],
        "o": "1*x",
        "t": "maximize",
    },
}

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

    is_retry = FEEDBACK_MARKER in user_message

    keyword = None
    for key in FIRST_MODEL:
        if key in user_message:
            keyword = key
            break

    if keyword is None:
        keyword = "工厂"

    if is_retry and keyword in CORRECT_MODEL:
        model = CORRECT_MODEL[keyword]
    else:
        model = FIRST_MODEL[keyword]

    # 模仿真实模型输出：```json 代码块 + 把 < > 转义成 \u003c \u003e
    body = json.dumps(model, ensure_ascii=False, separators=(",", ":"))
    body = body.replace("<", "\\u003c").replace(">", "\\u003e")
    content = "```json\n" + body + "\n```"

    print(json.dumps(build_response(content), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
