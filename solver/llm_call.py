#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LLM 调用脚本（被 C++ 层通过 popen 调用）。

用法:
    python llm_call.py <api_url> <system_prompt_b64> <user_message_b64>

API Key 通过环境变量传入（默认 ADPLANNER_LLM_API_KEY，回退 SILICONFLOW_API_KEY）。
修复前 Key 是作为命令行参数传进来的，会出现在进程列表里，这里已改为环境变量。
"""

import base64
import json
import os
import sys

import requests

KEY_ENV_VARS = ("ADPLANNER_LLM_API_KEY", "SILICONFLOW_API_KEY", "SILICONFLOW_KEY")
MODEL = "deepseek-ai/DeepSeek-V3"


def resolve_api_key():
    for name in KEY_ENV_VARS:
        value = os.environ.get(name)
        if value:
            return value
    return ""


def main():
    if len(sys.argv) < 4:
        print(json.dumps({
            "error": "用法: llm_call.py <api_url> <system_prompt_b64> <user_message_b64>"
        }, ensure_ascii=False))
        return 1

    api_url = sys.argv[1]
    try:
        system_prompt = base64.b64decode(sys.argv[2]).decode("utf-8")
        user_message = base64.b64decode(sys.argv[3]).decode("utf-8")
    except Exception as exc:
        print(json.dumps({"error": "参数解码失败: %s" % exc}, ensure_ascii=False))
        return 1

    api_key = resolve_api_key()
    if not api_key:
        print(json.dumps({
            "error": "未找到 API Key，请设置环境变量 %s" % KEY_ENV_VARS[0]
        }, ensure_ascii=False))
        return 1

    headers = {
        "Content-Type": "application/json",
        "Authorization": "Bearer %s" % api_key,
    }
    payload = {
        "model": MODEL,
        "max_tokens": 2048,
        "temperature": 0.0,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message},
        ],
    }

    try:
        resp = requests.post(api_url, headers=headers, json=payload, timeout=60)
    except Exception as exc:
        print(json.dumps({"error": "请求失败: %s" % exc}, ensure_ascii=False))
        return 1

    if resp.status_code != 200:
        # 让 C++ 层的「LLM 调用失败 → 重试」分支真正生效
        print(json.dumps({
            "error": "HTTP %d: %s" % (resp.status_code, resp.text[:500])
        }, ensure_ascii=False))
        return 1

    print(resp.text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
