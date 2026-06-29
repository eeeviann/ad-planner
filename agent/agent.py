

import os
import json
import requests
from solver.ip_solver import IntOptimizer


class AdPlanningAgent:


    SYSTEM_PROMPT = """你是一个专业的广告投放规划助手。用户会描述他们的广告投放需求，
你需要从描述中提取关键参数，然后调用求解器计算最优方案。

支持的参数（用户可能只提供部分参数，其余使用默认值）：
- 广告总预算（万元），默认400
- 策划预算（万元），默认100
- 电视广告单价（万元/个），默认30
- 儿童覆盖目标（百万人），默认5
- 家长覆盖目标（百万人），默认5
- 奖励预算（万元），默认149

广告媒体参数（固定）：
- 电视广告：单价30万，策划9万，奖励0，覆盖儿童1.2百万/家长0.2百万，曝光70万次
- 网站广告：单价15万，策划3万，奖励10万，覆盖儿童0.1百万/家长0.7百万，曝光100万次
- 微信广告：单价10万，策划2万，奖励10万，覆盖儿童0.2百万/家长0.8百万，曝光90万次

输出格式（严格遵循JSON，不要包含任何其他内容）：
{
  "ad_budget": 数值,
  "plan_budget": 数值,
  "tv_cost": 数值（如用户提到电视广告价格变化）,
  "child_coverage": 数值,
  "parent_coverage": 数值,
  "reward_budget": 数值
}
如果用户只是打招呼或询问，不输出JSON。"""

    def __init__(self, api_key: str = None):
        self.api_key = api_key or os.environ.get("SILICONFLOW_KEY", "")
        self.base_url = "https://api.siliconflow.cn/v1/chat/completions"
        self.solver = IntOptimizer()

    def _call_llm(self, user_message: str) -> str:

        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        }

        payload = {
            "model": "deepseek-ai/DeepSeek-V3",
            "messages": [
                {"role": "system", "content": self.SYSTEM_PROMPT},
                {"role": "user", "content": user_message}
            ],
            "temperature": 0.1,
            "max_tokens": 200
        }

        response = requests.post(self.base_url, headers=headers, json=payload, timeout=30)
        response.raise_for_status()
        return response.json()["choices"][0]["message"]["content"]

    def parse_parameters(self, user_message: str) -> dict:

        raw = self._call_llm(user_message)

        try:

            params = json.loads(raw)
            return params
        except json.JSONDecodeError:

            return None

    def solve_from_params(self, params: dict) -> dict:

        plan_budget = params.get("plan_budget", 100)
        ad_budget = params.get("ad_budget", 400)
        child_cov = params.get("child_coverage", 5)
        parent_cov = params.get("parent_coverage", 5)
        reward_budget = params.get("reward_budget", 149)
        tv_cost = params.get("tv_cost", 30)


        if plan_budget == 100 and tv_cost == 30:
            result = self.solver.solve_q1(ad_budget, plan_budget, child_cov, parent_cov, reward_budget)
            q_label = "第1问"
        elif plan_budget == 100 and tv_cost != 30:
            result = self.solver.solve_q2(ad_budget, plan_budget, tv_cost, child_cov, parent_cov, reward_budget)
            q_label = "第2问"
        else:
            result = self.solver.solve_q3(ad_budget, plan_budget, child_cov, parent_cov, reward_budget)
            q_label = "第3问"

        return result

    def format_response(self, result: dict, params: dict) -> str:

        q = result["question"]
        s = result["solution"]
        r = result

        return f"""
【{q}求解结果】
┌─────────────────────────────────────────┐
│  最优广告组合                             │
├─────────────────────────────────────────┤
│  电视广告 (x₁) = {s['x1']} 个                      │
│  网站广告 (x₂) = {s['x2']} 个                      │
│  微信广告 (x₃) = {s['x3']} 个                      │
├─────────────────────────────────────────┤
│  总曝光     = {r['total_exposure']} 万次               │
├─────────────────────────────────────────┤
│  约束满足情况                           │
│  广告成本   {r['ad_cost']:>4} 万  (上限 400万)         │
│  策划成本   {r['plan_cost']:>4} 万  (上限 {params.get('plan_budget',100):>3}万)     │
│  奖励成本   {r['reward_cost']:>4} 万  (上限 149万)       │
│  儿童覆盖   {r['child_coverage']:>4} 百万 (目标 {params.get('child_coverage',5)}百万)   │
│  家长覆盖   {r['parent_coverage']:>4} 百万 (目标 {params.get('parent_coverage',5)}百万)   │
│  搜索节点   {r['nodes_explored']:>5} 个 (枚举验证)       │
└─────────────────────────────────────────┘
"""

    def chat(self, user_message: str) -> str:

        params = self.parse_parameters(user_message)

        if params is None:

            return "你好！我是广告投放规划助手。你可以告诉我你的广告预算、策划预算等信息，我来帮你计算最优投放组合。"

        result = self.solve_from_params(params)
        return self.format_response(result, params)


def main():

    print("=" * 50)
    print("  广告投放智能规划Agent")
    print("  输入自然语言描述需求，自动求解最优组合")
    print("  输入 'quit' 退出")
    print("=" * 50)

    agent = AdPlanningAgent()


    if not agent.api_key:
        print("\n[提示] 未设置SILICONFLOW_API_KEY，将只展示本地求解演示")
        print("请到 https://www.siliconflow.cn/ 注册获取免费API Key")
        print("设置方式: export SILICONFLOW_KEY='your-key-here'\n")


        print("【本地求解演示】")
        print("假设策划预算100万，广告预算400万：")
        result = agent.solver.solve_q1()
        print(agent.format_response(result, {"plan_budget": 100, "child_coverage": 5, "parent_coverage": 5}))
        return

    while True:
        try:
            user_input = input("\n> ")
            if user_input.strip().lower() in ["quit", "exit", "q"]:
                break
            response = agent.chat(user_input)
            print(response)
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"\n[错误] {e}")


if __name__ == "__main__":
    main()
