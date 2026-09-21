#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
表达式解析单元测试。

重点是回归测试：修复前 parse_expr("0.08*2*a + 0.12*5*b + 0.09*3*c")
会返回 0.08（只取到第一个裸数字就 return），本文件把它钉死。

运行：
    python tests/test_parser.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "solver"))

from solve import ModelError, parse_constraint, parse_linear  # noqa: E402

FAILURES = []
CHECKS = [0]


def check(name, condition, detail=""):
    CHECKS[0] += 1
    if condition:
        print("  [PASS] %s" % name)
    else:
        print("  [FAIL] %s %s" % (name, detail))
        FAILURES.append(name)


def coeffs_of(expr, declared):
    return parse_linear(expr, set(declared)).coeffs


def main():
    print("=" * 68)
    print("  表达式解析单元测试")
    print("=" * 68)

    print("\n[1] 回归：混合系数的目标函数不再被截断")
    form = parse_linear("0.08*2*a + 0.12*5*b + 0.09*3*c", {"a", "b", "c"})
    check("a 的系数 = 0.16", abs(form.coeffs.get("a", 0) - 0.16) < 1e-12,
          "实际 %r" % form.coeffs.get("a"))
    check("b 的系数 = 0.6", abs(form.coeffs.get("b", 0) - 0.6) < 1e-12,
          "实际 %r" % form.coeffs.get("b"))
    check("c 的系数 = 0.27", abs(form.coeffs.get("c", 0) - 0.27) < 1e-12,
          "实际 %r" % form.coeffs.get("c"))
    check("常数项为 0", abs(form.const) < 1e-12, "实际 %r" % form.const)

    print("\n[2] 基本形式")
    check("3*x + 5*y", coeffs_of("3*x + 5*y", "xy") == {"x": 3.0, "y": 5.0})
    check("省略系数的 x 视为 1", coeffs_of("x", "x") == {"x": 1.0})
    check("2*x - 3*y", coeffs_of("2*x - 3*y", "xy") == {"x": 2.0, "y": -3.0})
    check("-0.5*x", coeffs_of("-0.5*x", "x") == {"x": -0.5})
    check("x/2", coeffs_of("x/2", "x") == {"x": 0.5})
    check("2*(x + 1) 的常数项", abs(parse_linear("2*(x + 1)", {"x"}).const - 2.0) < 1e-12)
    check("(a + b)*2", coeffs_of("(a + b)*2", "ab") == {"a": 2.0, "b": 2.0})
    check("1e-3*x 科学计数法",
          abs(coeffs_of("1e-3*x", "x").get("x", 0) - 0.001) < 1e-15)

    print("\n[3] 常数项与变量混合")
    form = parse_linear("3*x + 5*y + 7", {"x", "y"})
    check("变量系数正确", form.coeffs == {"x": 3.0, "y": 5.0})
    check("常数项 = 7", abs(form.const - 7.0) < 1e-12)

    print("\n[4] 纯常数表达式")
    form = parse_linear("12", set())
    check("无变量", form.coeffs == {})
    check("值 = 12", abs(form.const - 12.0) < 1e-12)

    print("\n[5] 应当被拒绝的输入")
    for bad, why in [
        ("x*y", "变量×变量（非线性）"),
        ("x/z", "除以变量"),
        ("x/0", "除以 0"),
        ("z + 1", "未声明的变量"),
        ("3*x +", "语法不完整"),
    ]:
        try:
            parse_linear(bad, {"x", "y"})
            check("拒绝 %r（%s）" % (bad, why), False, "却解析成功了")
        except ModelError as exc:
            check("拒绝 %r（%s）" % (bad, why), True)
            print("         原因: %s" % exc)

    print("\n[6] 约束拆解")
    diff, op = parse_constraint("2*x + 1*y <= 12", {"x", "y"})
    check("运算符 = <=", op == "<=", "实际 %r" % op)
    check("移项后系数", diff.coeffs == {"x": 2.0, "y": 1.0})
    check("移项后常数 = -12", abs(diff.const + 12.0) < 1e-12)

    diff, op = parse_constraint("b >= c", {"b", "c"})
    check("b >= c 的运算符", op == ">=")
    check("b >= c 的系数", diff.coeffs == {"b": 1.0, "c": -1.0})

    diff, op = parse_constraint("x + y = 10", {"x", "y"})
    check("等号约束", op == "=" and diff.coeffs == {"x": 1.0, "y": 1.0})

    diff, op = parse_constraint("x + y == 10", {"x", "y"})
    check("双等号约束", op == "=" and diff.coeffs == {"x": 1.0, "y": 1.0})

    diff, op = parse_constraint("2*a + 5*b ≤ 500", {"a", "b"})
    check("Unicode ≤ 被识别", op == "<=" and diff.coeffs == {"a": 2.0, "b": 5.0})

    diff, op = parse_constraint("2*a + 5*b ≥ 500", {"a", "b"})
    check("Unicode ≥ 被识别", op == ">=")

    print("\n[7] 两侧都含变量")
    diff, op = parse_constraint("x + 2*y <= 3*x", {"x", "y"})
    check("x + 2*y <= 3*x", diff.coeffs == {"x": -2.0, "y": 2.0},
          "实际 %r" % diff.coeffs)

    print("\n" + "=" * 68)
    if FAILURES:
        print("  失败 %d / %d 项: %s" % (len(FAILURES), CHECKS[0], ", ".join(FAILURES)))
        print("=" * 68)
        return 1
    print("  全部 %d 项通过" % CHECKS[0])
    print("=" * 68)
    return 0


if __name__ == "__main__":
    sys.exit(main())
