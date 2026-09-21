#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ad-planner 求解层（Python "计算器"）。

职责边界：只做「线性 / 整数线性模型 → 求解」，不含任何 Agent 逻辑。
C++ 层通过 stdin 传入 JSON 模型，本脚本把结果以 JSON 打到 stdout。

输入 JSON:
{
  "variables":     ["a", "b", "c"],
  "constraints":   ["2*a + 5*b + 3*c <= 500", "a + b + c <= 150", "b >= c"],
  "objective":     "0.16*a + 0.6*b + 0.27*c",
  "type":          "maximize",
  "integer_vars":  ["a", "b", "c"]
}

输出 JSON:
  {"status":"OPTIMAL","solution":{"a":0,...},"objective_value":60.0,...}
  {"status":"ERROR","message":"<可读的建模错误>"}   # 供 C++ Agent 反馈给 LLM 重试

表达式解析说明（2026-09 重构）
------------------------------------------------------------------
旧实现用一条正则逐段匹配表达式，其中"独立数字"分支一旦命中就 `return`，
于是 "0.08*2*a + 0.12*5*b + 0.09*3*c" 只解析出第一个裸数字 0.08 便返回，
目标函数被静默截断成常数（详见 docs/BUGFIX-expression-parser.md）。

现改为基于 ast 的安全解析：
  1. 先做 AST 白名单校验，只允许 数字 / 变量 / + - * / / 括号 / 一元正负号；
  2. 再在 AST 上按线性代数规则求值，得到 {变量: 系数} + 常数项。
不使用 eval()，因此也不会执行来自 LLM 的任意代码。
"""

import ast
import json
import sys

from ortools.linear_solver import pywraplp

TOL = 1e-9
_OPS = ("<=", ">=", "=")


class ModelError(ValueError):
    """建模或解析错误。会转成结构化 JSON 回传，触发 C++ Agent 的重试。"""


class LinearForm:
    """线性表达式 sum(c_i * x_i) + const。"""

    __slots__ = ("coeffs", "const")

    def __init__(self, coeffs=None, const=0.0):
        self.coeffs = {}
        if coeffs:
            for name, c in coeffs.items():
                if abs(c) > TOL:
                    self.coeffs[name] = float(c)
        self.const = float(const)

    def iadd(self, other, scale=1.0):
        """self += scale * other"""
        for name, c in other.coeffs.items():
            new = self.coeffs.get(name, 0.0) + scale * c
            if abs(new) > TOL:
                self.coeffs[name] = new
            elif name in self.coeffs:
                del self.coeffs[name]
        self.const += scale * other.const
        return self

    def scaled(self, factor):
        return LinearForm({k: v * factor for k, v in self.coeffs.items()},
                          self.const * factor)

    def is_constant(self):
        return not self.coeffs

    def __str__(self):
        if not self.coeffs:
            return "%g" % self.const
        parts = ["%g*%s" % (c, n) for n, c in sorted(self.coeffs.items())]
        if self.const:
            parts.append("%g" % self.const)
        return " + ".join(parts)


# --------------------------------------------------------------------------
# 归一化：把 LLM 可能输出的全角/Unicode 符号转成 ASCII
# --------------------------------------------------------------------------

_TRANS = {
    "\u2264": "<=", "\u2265": ">=", "\u00d7": "*", "\u00f7": "/",
    "\u2212": "-", "\uff08": "(", "\uff09": ")", "\uff0c": ",",
    "\u3002": ".", "\uff1b": ";", "\uff1d": "=",
}


def normalize(text):
    text = str(text)
    for src, dst in _TRANS.items():
        text = text.replace(src, dst)
    return text


def normalize_relation(text):
    """把各种比较写法统一成 <= / >= / =。"""
    text = normalize(text)
    text = text.replace("==", "=")
    if "<=" in text or ">=" in text or "=" in text:
        return text
    # 只出现 < 或 > 的情况（LLM 偶尔会写 a < b）
    if "<" in text or ">" in text:
        return text.replace("<", "<=").replace(">", ">=")
    return text


# --------------------------------------------------------------------------
# 表达式解析
# --------------------------------------------------------------------------

def parse_linear(expr, var_names):
    """把线性表达式字符串解析成 LinearForm。"""
    if expr is None:
        raise ModelError("表达式为空")
    text = normalize(expr).strip()
    if not text:
        raise ModelError("表达式为空")
    try:
        tree = ast.parse(text, mode="eval")
    except SyntaxError as exc:
        raise ModelError("无法解析表达式 %r: %s" % (text, exc.msg))
    return _eval_node(tree.body, var_names, text)


def _eval_node(node, var_names, src):
    if isinstance(node, ast.Constant):
        if isinstance(node.value, bool) or not isinstance(node.value, (int, float)):
            raise ModelError("表达式 %r 中出现非法常量 %r" % (src, node.value))
        return LinearForm(const=float(node.value))

    if isinstance(node, ast.Name):
        if node.id not in var_names:
            raise ModelError(
                "表达式 %r 中的变量 %r 未在 variables 中声明；已声明变量: %s"
                % (src, node.id, sorted(var_names)))
        return LinearForm({node.id: 1.0})

    if isinstance(node, ast.UnaryOp):
        operand = _eval_node(node.operand, var_names, src)
        if isinstance(node.op, ast.UAdd):
            return operand
        if isinstance(node.op, ast.USub):
            return operand.scaled(-1.0)
        raise ModelError("表达式 %r 中出现了不支持的一元运算符" % src)

    if isinstance(node, ast.BinOp):
        left = _eval_node(node.left, var_names, src)
        right = _eval_node(node.right, var_names, src)
        if isinstance(node.op, ast.Add):
            return left.iadd(right)
        if isinstance(node.op, ast.Sub):
            return left.iadd(right, -1.0)
        if isinstance(node.op, ast.Mult):
            if left.is_constant():
                return right.scaled(left.const)
            if right.is_constant():
                return left.scaled(right.const)
            raise ModelError("表达式 %r 是二次的（变量×变量），本求解器仅支持线性模型" % src)
        if isinstance(node.op, ast.Div):
            if not right.is_constant():
                raise ModelError("表达式 %r 中不允许除以含变量的式子" % src)
            if abs(right.const) <= TOL:
                raise ModelError("表达式 %r 中出现了除以 0" % src)
            return left.scaled(1.0 / right.const)
        raise ModelError("表达式 %r 中出现了不支持的运算符" % src)

    raise ModelError("表达式 %r 中出现了不支持的语法: %s" % (src, type(node).__name__))


def parse_constraint(constraint, var_names):
    """返回 (LinearForm(diff), op)，语义为 diff op 0。"""
    if constraint is None:
        raise ModelError("约束为空")
    text = normalize_relation(constraint).strip()
    if not text:
        raise ModelError("约束为空")

    for op in _OPS:
        idx = text.find(op)
        if idx != -1:
            lhs_text, rhs_text = text[:idx], text[idx + len(op):]
            break
    else:
        raise ModelError("约束 %r 中没有比较运算符（<=、>=、=）" % constraint)

    lhs = parse_linear(lhs_text, var_names)
    rhs = parse_linear(rhs_text, var_names)
    # 统一挪到左边：diff <= 0 / diff >= 0 / diff == 0
    return lhs.iadd(rhs, -1.0), op


def _to_ortools(lin, var_objs):
    """LinearForm → ortools 表达式。"""
    total = None
    for name, c in lin.coeffs.items():
        term = c * var_objs[name]
        total = term if total is None else total + term
    if total is None:
        return lin.const
    if lin.const:
        total = total + lin.const
    return total


# --------------------------------------------------------------------------
# 求解
# --------------------------------------------------------------------------

def solve_ip(variables, constraints, objective, obj_type, integer_vars):
    if not variables:
        raise ModelError("variables 为空")

    solver = pywraplp.Solver.CreateSolver("SCIP")
    if solver is None:
        raise ModelError("无法创建 SCIP 求解器，请确认已正确安装 ortools")

    var_names = set(variables)
    integer_vars = set(integer_vars)

    # 变量默认下界 0、上界 +inf；这一约定写在 README「支持范围」中
    var_objs = {}
    for name in variables:
        if name in integer_vars:
            var_objs[name] = solver.IntVar(0.0, solver.infinity(), name)
        else:
            var_objs[name] = solver.NumVar(0.0, solver.infinity(), name)

    parsed_constraints = []
    for c in constraints:
        diff, op = parse_constraint(c, var_names)
        parsed_constraints.append((diff, op))

    for diff, op in parsed_constraints:
        expr = _to_ortools(diff, var_objs)
        if op == "<=":
            solver.Add(expr <= 0)
        elif op == ">=":
            solver.Add(expr >= 0)
        else:
            solver.Add(expr == 0)

    obj_lin = parse_linear(objective, var_names)
    obj_expr = _to_ortools(obj_lin, var_objs)

    kind = str(obj_type or "maximize").strip().lower()
    if kind in ("max", "maximize", "maximise", "maximum", "maximization"):
        solver.Maximize(obj_expr)
        direction = "maximize"
    elif kind in ("min", "minimize", "minimise", "minimum", "minimization"):
        solver.Minimize(obj_expr)
        direction = "minimize"
    else:
        raise ModelError("无法识别的目标类型 %r，应为 maximize 或 minimize" % obj_type)

    status = solver.Solve()
    status_map = {
        pywraplp.Solver.OPTIMAL: "OPTIMAL",
        pywraplp.Solver.FEASIBLE: "FEASIBLE",
        pywraplp.Solver.INFEASIBLE: "INFEASIBLE",
        pywraplp.Solver.UNBOUNDED: "UNBOUNDED",
        pywraplp.Solver.ABNORMAL: "ABNORMAL",
    }
    status_name = status_map.get(status, "UNKNOWN")

    out = {
        "status": status_name,
        "direction": direction,
        "solution": {},
        "objective_value": 0.0,
        # 回传解析后的规范模型，便于人工核查「LLM 到底建了什么模」
        "normalized_model": {
            "objective": str(obj_lin),
            "constraints": ["%s %s 0" % (str(diff), op) for diff, op in parsed_constraints],
        },
    }

    if status_name in ("OPTIMAL", "FEASIBLE"):
        out["solution"] = {v: var_objs[v].solution_value() for v in variables}
        out["objective_value"] = solver.Objective().Value()
    elif status_name == "INFEASIBLE":
        out["message"] = "约束相互矛盾，模型不可行"
    elif status_name == "UNBOUNDED":
        out["message"] = "目标函数无界，请检查是否遗漏上界约束"
    else:
        out["message"] = "求解器返回状态 %s" % status_name

    return out


# --------------------------------------------------------------------------
# 入口
# --------------------------------------------------------------------------

def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False))
    sys.stdout.write("\n")
    sys.stdout.flush()


def main():
    raw = sys.stdin.read()
    try:
        data = json.loads(raw)
    except Exception:
        emit({"status": "ERROR", "message": "输入不是合法 JSON"})
        return 0

    if not isinstance(data, dict):
        emit({"status": "ERROR", "message": "输入 JSON 顶层必须是对象"})
        return 0

    variables = data.get("variables") or []
    constraints = data.get("constraints") or []
    objective = data.get("objective", "")
    obj_type = data.get("type", "maximize")
    integer_vars = data.get("integer_vars")
    if integer_vars is None:
        integer_vars = list(variables)

    if not constraints:
        emit({"status": "ERROR", "message": "constraints 为空"})
        return 0

    try:
        result = solve_ip(variables, constraints, objective, obj_type, integer_vars)
    except ModelError as exc:
        # 关键：不抛裸异常、不打印 traceback 到 stdout。
        # 干净的 message 让 C++ 层能把「哪里错了」原样回填给 LLM 重试，
        # 而不是像修复前那样只拿到一个 "ERROR"。
        emit({"status": "ERROR", "message": str(exc)})
        return 0
    except Exception as exc:  # 兜底，保证 stdout 永远是合法 JSON
        emit({"status": "ERROR", "message": "求解层内部错误: %s: %s"
                                        % (type(exc).__name__, exc)})
        return 0

    emit(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
