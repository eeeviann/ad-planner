#ifndef LINEAR_EXPR_H
#define LINEAR_EXPR_H

#include <map>
#include <string>
#include <vector>

// 线性表达式：sum(coeffs[v] * v) + constant
struct LinearForm {
    std::map<std::string, double> coeffs;
    double constant = 0.0;

    bool is_constant() const { return coeffs.empty(); }

    // 代入一组变量取值，算出表达式的数值
    double evaluate(const std::map<std::string, double>& assignment) const;

    // 归一化后的可读形式，如 "0.16*a + 0.6*b + 0.27*c"
    std::string to_string() const;
};

// 把全角符号与 Unicode 数学符号（≤ ≥ × ÷ − （ ） ，）统一成 ASCII
std::string normalize_math_text(const std::string& text);

// 解析线性表达式。变量必须出现在 allowed_vars 中，否则视为建模错误。
// 支持：数字（含小数与科学计数法）、变量名（字母/下划线开头）、
//       + - * / 、一元正负号、圆括号。
// 不支持：变量×变量（非线性）、除以含变量的式子。
bool parse_linear_expression(const std::string& expr,
                             const std::vector<std::string>& allowed_vars,
                             LinearForm& out,
                             std::string& error);

// 把 "lhs OP rhs" 拆成三段，op 取 "<=" / ">=" / "="。
bool split_relation(const std::string& text,
                    std::string& lhs,
                    std::string& op,
                    std::string& rhs,
                    std::string& error);

#endif  // LINEAR_EXPR_H
