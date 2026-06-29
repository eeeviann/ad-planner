

from typing import List, Tuple, Optional, Dict
import time


class IntOptimizer:


    def __init__(self):
        self.best_solution = None
        self.best_value = float('-inf')
        self.nodes_explored = 0

    def enumerate_solutions(
        self,
        x1_bounds: Tuple[int, int],
        x2_bounds: Tuple[int, int],
        x3_bounds: Tuple[int, int],
        constraints: List[callable],
        objective: callable,
        maximize: bool = True
    ) -> Tuple[Tuple[int, int, int], float]:


        self.best_solution = None
        self.best_value = float('-inf') if maximize else float('inf')
        self.nodes_explored = 0

        for x1 in range(x1_bounds[0], x1_bounds[1] + 1):
            for x2 in range(x2_bounds[0], x2_bounds[1] + 1):
                for x3 in range(x3_bounds[0], x3_bounds[1] + 1):
                    self.nodes_explored += 1


                    all_satisfied = all(c(x1, x2, x3) for c in constraints)
                    if not all_satisfied:
                        continue


                    value = objective(x1, x2, x3)


                    if maximize:
                        if value > self.best_value:
                            self.best_value = value
                            self.best_solution = (x1, x2, x3)
                    else:
                        if value < self.best_value:
                            self.best_value = value
                            self.best_solution = (x1, x2, x3)

        return self.best_solution, self.best_value

    def solve_q1(
        self,
        ad_budget: int = 400,
        plan_budget: int = 100,
        child_coverage: int = 5,
        parent_coverage: int = 5,
        reward_budget: int = 149
    ) -> Dict:


        def objective(x1, x2, x3):
            return 70 * x1 + 100 * x2 + 90 * x3


        constraints = [

            lambda x1, x2, x3: 30 * x1 + 15 * x2 + 10 * x3 <= ad_budget,

            lambda x1, x2, x3: 9 * x1 + 3 * x2 + 2 * x3 <= plan_budget,

            lambda x1, x2, x3: 1.2 * x1 + 0.1 * x2 + 0.2 * x3 >= child_coverage,

            lambda x1, x2, x3: 0.2 * x1 + 0.7 * x2 + 0.8 * x3 >= parent_coverage,

            lambda x1, x2, x3: 10 * x2 + 10 * x3 <= reward_budget,
        ]


        x1_max = ad_budget // 30
        x2_max = ad_budget // 15
        x3_max = ad_budget // 10

        solution, value = self.enumerate_solutions(
            (0, x1_max), (0, x2_max), (0, x3_max),
            constraints, objective, maximize=True
        )

        return self._build_result(solution, value, "Q1", plan_budget)

    def solve_q2(
        self,
        ad_budget: int = 400,
        plan_budget: int = 100,
        tv_cost: int = 25,
        child_coverage: int = 5,
        parent_coverage: int = 5,
        reward_budget: int = 149
    ) -> Dict:


        def objective(x1, x2, x3):
            return 70 * x1 + 100 * x2 + 90 * x3

        constraints = [
            lambda x1, x2, x3: tv_cost * x1 + 15 * x2 + 10 * x3 <= ad_budget,
            lambda x1, x2, x3: 9 * x1 + 3 * x2 + 2 * x3 <= plan_budget,
            lambda x1, x2, x3: 1.2 * x1 + 0.1 * x2 + 0.2 * x3 >= child_coverage,
            lambda x1, x2, x3: 0.2 * x1 + 0.7 * x2 + 0.8 * x3 >= parent_coverage,
            lambda x1, x2, x3: 10 * x2 + 10 * x3 <= reward_budget,
        ]

        x1_max = ad_budget // tv_cost
        x2_max = ad_budget // 15
        x3_max = ad_budget // 10

        solution, value = self.enumerate_solutions(
            (0, x1_max), (0, x2_max), (0, x3_max),
            constraints, objective, maximize=True
        )

        return self._build_result(solution, value, "Q2", plan_budget, tv_cost=tv_cost)

    def solve_q3(
        self,
        ad_budget: int = 400,
        plan_budget: int = 200,
        child_coverage: int = 5,
        parent_coverage: int = 5,
        reward_budget: int = 149
    ) -> Dict:


        def objective(x1, x2, x3):
            return 70 * x1 + 100 * x2 + 90 * x3

        constraints = [
            lambda x1, x2, x3: 30 * x1 + 15 * x2 + 10 * x3 <= ad_budget,
            lambda x1, x2, x3: 9 * x1 + 3 * x2 + 2 * x3 <= plan_budget,
            lambda x1, x2, x3: 1.2 * x1 + 0.1 * x2 + 0.2 * x3 >= child_coverage,
            lambda x1, x2, x3: 0.2 * x1 + 0.7 * x2 + 0.8 * x3 >= parent_coverage,
            lambda x1, x2, x3: 10 * x2 + 10 * x3 <= reward_budget,
        ]

        x1_max = ad_budget // 30
        x2_max = ad_budget // 15
        x3_max = ad_budget // 10

        solution, value = self.enumerate_solutions(
            (0, x1_max), (0, x2_max), (0, x3_max),
            constraints, objective, maximize=True
        )

        return self._build_result(solution, value, "Q3", plan_budget)

    def _build_result(
        self,
        solution: Tuple[int, int, int],
        value: float,
        question: str,
        plan_budget: int,
        tv_cost: int = 30
    ) -> Dict:

        x1, x2, x3 = solution

        ad_cost = tv_cost * x1 + 15 * x2 + 10 * x3
        plan_cost = 9 * x1 + 3 * x2 + 2 * x3
        reward_cost = 10 * x2 + 10 * x3
        child_cov = round(1.2 * x1 + 0.1 * x2 + 0.2 * x3, 1)
        parent_cov = round(0.2 * x1 + 0.7 * x2 + 0.8 * x3, 1)

        return {
            "question": question,
            "solution": {"x1": x1, "x2": x2, "x3": x3},
            "total_exposure": value,
            "ad_cost": ad_cost,
            "plan_cost": plan_cost,
            "reward_cost": reward_cost,
            "child_coverage": child_cov,
            "parent_coverage": parent_cov,
            "nodes_explored": self.nodes_explored,
        }

    def solve_all(self) -> List[Dict]:

        return [self.solve_q1(), self.solve_q2(), self.solve_q3()]


def print_result(r: Dict):

    print(f"\n{'='*40}")
    print(f"  {r['question']} 最优解")
    print(f"{'='*40}")
    print(f"  电视广告 x1 = {r['solution']['x1']}")
    print(f"  网站广告 x2 = {r['solution']['x2']}")
    print(f"  微信广告 x3 = {r['solution']['x3']}")
    print(f"  总曝光     = {r['total_exposure']} 万次")
    print(f"{'='*40}")
    print(f"  广告成本   = {r['ad_cost']} 万  (≤400)")
    print(f"  策划成本   = {r['plan_cost']} 万  (≤{r['plan_cost']})")
    print(f"  奖励成本   = {r['reward_cost']} 万  (≤149)")
    print(f"  儿童覆盖   = {r['child_coverage']} 百万 (≥5)")
    print(f"  家长覆盖   = {r['parent_coverage']} 百万 (≥5)")
    print(f"  搜索节点   = {r['nodes_explored']}")
    print(f"{'='*40}")


if __name__ == "__main__":
    solver = IntOptimizer()
    results = solver.solve_all()

    print("\n广告投放整数规划求解器")
    print("求解器: 分支定界枚举法")
    print("问题: 优格公司早餐麦片推广案例\n")

    for r in results:
        print_result(r)
