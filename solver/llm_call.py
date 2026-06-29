

import json
import sys
import base64
import requests


def main():
    if len(sys.argv) < 4:
        print(json.dumps({"error": "Usage: llm_call.py <api_url> <api_key> <system_b64> <user_b64>"}))
        return

    api_url = sys.argv[1]
    api_key = sys.argv[2]
    system_prompt = base64.b64decode(sys.argv[3]).decode("utf-8")
    user_message = base64.b64decode(sys.argv[4]).decode("utf-8")

    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}"
    }

    payload = {
        "model": "deepseek-ai/DeepSeek-V3",
        "max_tokens": 2048,
        "temperature": 0.0,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message}
        ]
    }

    try:
        resp = requests.post(api_url, headers=headers, json=payload, timeout=60)
        print(resp.text)
    except Exception as e:
        print(json.dumps({"error": str(e)}))


if __name__ == "__main__":
    main()
