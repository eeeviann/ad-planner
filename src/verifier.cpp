#include "verifier.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "linear_expr.h"

namespace {

std::string num(double v) {
    std::ostringstream ss;
    ss << std::setprecision(10) << v;
    return ss.str();
}

}  // namespace

bool Verifier::verify_integer(const std::vector<std::string>& integer_vars,
                              const std::map<std::string, double>& assignment,
                              double tolerance,
                              std::string& error) {
    for (const std::string& name : integer_vars) {
        const auto it = assignment.find(name);
        if (it == assignment.end()) {
            error = "整数变量 '" + name + "' 在解向量中缺失";
            return false;
        }
        const double rounded = std::round(it->second);
        if (std::fabs(it->second - rounded) > tolerance) {
            error = "整数变量 '" + name + "' 的取值 " + num(it->second) +
                    " 不是整数";
            return false;
        }
    }
    return true;
}

VerificationReport Verifier::verify_solution(
    const std::vector<std::string>& variables,
    const std::vector<std::string>& integer_vars,
    const std::vector<std::string>& constraints,
    const std::string& objective,
    const std::map<std::string, double>& assignment,
    double reported_objective,
    double tolerance) {
    VerificationReport report;

    // ---- 1. 结构检查 -------------------------------------------------
    for (const std::string& name : variables) {
        const auto it = assignment.find(name);
        if (it == assignment.end()) {
            report.message = "解向量中缺少变量 '" + name + "'";
            report.notes.push_back(report.message);
            return report;
        }
        if (std::isnan(it->second) || std::isinf(it->second)) {
            report.message = "变量 '" + name + "' 的取值不是有限数";
            report.notes.push_back(report.message);
            return report;
        }
    }
    report.notes.push_back("结构检查通过：" + std::to_string(variables.size()) +
                           " 个变量取值均有限");

    // ---- 2. 整数性检查 -----------------------------------------------
    std::string int_error;
    if (!verify_integer(integer_vars, assignment, tolerance, int_error)) {
        report.message = "整数性检查失败：" + int_error;
        report.notes.push_back(report.message);
        return report;
    }
    report.notes.push_back("整数性检查通过：" + std::to_string(integer_vars.size()) +
                           " 个整数变量取值均为整数");

    // ---- 3. 约束检查 -------------------------------------------------
    int passed = 0;
    for (size_t i = 0; i < constraints.size(); ++i) {
        const std::string& raw = constraints[i];

        std::string lhs_text, op, rhs_text, split_error;
        if (!split_relation(raw, lhs_text, op, rhs_text, split_error)) {
            report.message = "约束 " + std::to_string(i + 1) + " 无法解析：" + split_error;
            report.notes.push_back(report.message);
            return report;
        }

        LinearForm lhs, rhs;
        std::string parse_error;
        if (!parse_linear_expression(lhs_text, variables, lhs, parse_error)) {
            report.message = "约束 " + std::to_string(i + 1) + " 左端无法解析：" + parse_error;
            report.notes.push_back(report.message);
            return report;
        }
        if (!parse_linear_expression(rhs_text, variables, rhs, parse_error)) {
            report.message = "约束 " + std::to_string(i + 1) + " 右端无法解析：" + parse_error;
            report.notes.push_back(report.message);
            return report;
        }

        const double lhs_value = lhs.evaluate(assignment);
        const double rhs_value = rhs.evaluate(assignment);
        const double diff = lhs_value - rhs_value;

        bool satisfied = false;
        if (op == "<=") {
            satisfied = diff <= tolerance;
        } else if (op == ">=") {
            satisfied = diff >= -tolerance;
        } else {
            satisfied = std::fabs(diff) <= tolerance;
        }

        if (!satisfied) {
            report.violated_constraint_index = static_cast<int>(i + 1);
            report.violated_constraint = raw;
            report.message = "约束 " + std::to_string(i + 1) + " 不被满足：" + raw +
                             "（代入解后 左端=" + num(lhs_value) +
                             "，右端=" + num(rhs_value) + "）";
            report.notes.push_back(report.message);
            return report;
        }
        ++passed;
    }
    report.notes.push_back("约束检查通过：" + std::to_string(passed) + "/" +
                           std::to_string(constraints.size()) + " 条约束全部满足");

    // ---- 4. 目标值检查 -----------------------------------------------
    LinearForm obj;
    std::string obj_error;
    if (!parse_linear_expression(objective, variables, obj, obj_error)) {
        report.message = "目标函数无法解析：" + obj_error;
        report.notes.push_back(report.message);
        return report;
    }

    const double recomputed = obj.evaluate(assignment);
    if (std::isnan(recomputed)) {
        report.message = "目标函数重算时缺少变量取值";
        report.notes.push_back(report.message);
        return report;
    }

    report.recomputed_objective = recomputed;
    const double gap = std::fabs(recomputed - reported_objective);
    if (gap > tolerance) {
        report.message = "目标值与解不一致：求解器报告 " + num(reported_objective) +
                         "，用解向量重算得到 " + num(recomputed);
        report.notes.push_back(report.message);
        return report;
    }

    report.objective_checked = true;
    report.notes.push_back("目标值检查通过：重算 " + num(recomputed) +
                           " 与报告值一致");

    report.ok = true;
    report.message = "验证通过";
    return report;
}
