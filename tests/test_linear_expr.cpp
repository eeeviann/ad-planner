// C++ 侧单元测试：线性表达式解析 + 解验证。
//
// 编译运行（在仓库根目录）：
//     g++ -std=c++17 -Wall -Isrc -o test_core.exe tests/test_linear_expr.cpp \
//         src/linear_expr.cpp src/verifier.cpp
//     ./test_core.exe
// 或直接用：make test-cpp

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "linear_expr.h"
#include "verifier.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const std::string& name, bool ok, const std::string& detail = "") {
    ++g_checks;
    if (ok) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name;
        if (!detail.empty()) std::cout << "   " << detail;
        std::cout << "\n";
        ++g_failures;
    }
}

bool nearly(double a, double b, double tol = 1e-9) {
    return std::fabs(a - b) <= tol;
}

const std::vector<std::string> ABC = {"a", "b", "c"};
const std::vector<std::string> XY = {"x", "y"};

void test_parse_regression() {
    std::cout << "\n[1] 回归：混合系数表达式不再被截断\n";

    LinearForm form;
    std::string error;
    const bool ok = parse_linear_expression(
        "0.08*2*a + 0.12*5*b + 0.09*3*c", ABC, form, error);
    check("解析成功", ok, error);
    check("a 的系数 = 0.16", nearly(form.coeffs["a"], 0.16),
          "实际 " + std::to_string(form.coeffs["a"]));
    check("b 的系数 = 0.6", nearly(form.coeffs["b"], 0.6));
    check("c 的系数 = 0.27", nearly(form.coeffs["c"], 0.27));
    check("常数项为 0", nearly(form.constant, 0.0));
    check("可读形式正确", form.to_string() == "0.16*a + 0.6*b + 0.27*c",
          "实际 " + form.to_string());
}

void test_parse_basic() {
    std::cout << "\n[2] 基本形式\n";
    LinearForm f;
    std::string e;

    check("3*x + 5*y", parse_linear_expression("3*x + 5*y", XY, f, e) &&
                           nearly(f.coeffs["x"], 3) && nearly(f.coeffs["y"], 5), e);
    check("省略系数 x 视为 1",
          parse_linear_expression("x", XY, f, e) && nearly(f.coeffs["x"], 1), e);
    check("2*x - 3*y",
          parse_linear_expression("2*x - 3*y", XY, f, e) &&
              nearly(f.coeffs["x"], 2) && nearly(f.coeffs["y"], -3), e);
    check("-0.5*x", parse_linear_expression("-0.5*x", XY, f, e) &&
                        nearly(f.coeffs["x"], -0.5), e);
    check("x/2", parse_linear_expression("x/2", XY, f, e) &&
                     nearly(f.coeffs["x"], 0.5), e);
    check("2*(x + 1) 常数项 = 2",
          parse_linear_expression("2*(x + 1)", XY, f, e) &&
              nearly(f.constant, 2) && nearly(f.coeffs["x"], 2), e);
    check("(a + b)*2", parse_linear_expression("(a + b)*2", ABC, f, e) &&
                           nearly(f.coeffs["a"], 2) && nearly(f.coeffs["b"], 2), e);
    check("x + 2*y - 3*z + 7 的常数项",
          parse_linear_expression("3*x + 5*y + 7", XY, f, e) &&
              nearly(f.constant, 7), e);
}

void test_parse_rejections() {
    std::cout << "\n[3] 应当被拒绝的输入\n";
    LinearForm f;
    std::string e;

    check("拒绝 x*y（非线性）", !parse_linear_expression("x*y", XY, f, e), "却通过了");
    check("拒绝 x/0（除以 0）", !parse_linear_expression("x/0", XY, f, e), "却通过了");
    check("拒绝未声明变量 z", !parse_linear_expression("z + 1", XY, f, e), "却通过了");
    check("拒绝语法错误 3*x +",
          !parse_linear_expression("3*x +", XY, f, e), "却通过了");
    check("拒绝括号不匹配 (x + 1",
          !parse_linear_expression("(x + 1", XY, f, e), "却通过了");
}

void test_split_relation() {
    std::cout << "\n[4] 约束拆解\n";
    std::string lhs, op, rhs, error;

    check("2*x <= 12",
          split_relation("2*x <= 12", lhs, op, rhs, error) && op == "<=", error);
    check("b >= c", split_relation("b >= c", lhs, op, rhs, error) && op == ">=",
          error);
    check("x + y = 10", split_relation("x + y = 10", lhs, op, rhs, error) &&
                            op == "=", error);
    check("x + y == 10", split_relation("x + y == 10", lhs, op, rhs, error) &&
                             op == "=", error);
    check("Unicode 小于等于号", split_relation("2*a + 5*b \xE2\x89\xA4 500", lhs, op,
                                              rhs, error) && op == "<=", error);
    check("拒绝没有比较符的表达式",
          !split_relation("2*x + 3", lhs, op, rhs, error), "却通过了");
}

// 这是修复的重点：验证器必须真的把解代入约束
void test_verifier() {
    std::cout << "\n[5] 解验证\n";

    const std::vector<std::string> vars = {"a", "b", "c"};
    const std::vector<std::string> ints = {"a", "b", "c"};
    const std::vector<std::string> cons = {
        "2*a + 5*b + 3*c <= 500",
        "a + b + c <= 150",
        "b >= c",
    };
    const std::string obj = "0.16*a + 0.6*b + 0.27*c";

    // 5.1 正确解应当通过
    {
        std::map<std::string, double> sol = {{"a", 0}, {"b", 100}, {"c", 0}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 60.0);
        check("正确解 (0,100,0) 通过", r.ok, r.message);
        check("重算目标值 = 60", nearly(r.recomputed_objective, 60.0),
              std::to_string(r.recomputed_objective));
    }

    // 5.2 违反约束的解必须被抓住（这条在修复前会误判为通过）
    {
        std::map<std::string, double> sol = {{"a", 0}, {"b", 200}, {"c", 0}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 120.0);
        check("不可行解 (0,200,0) 被拒绝", !r.ok, r.message);
        check("指出了违约约束序号 1", r.violated_constraint_index == 1,
              std::to_string(r.violated_constraint_index));
    }

    // 5.3 目标值与解不匹配必须被抓住（0.08 那类静默错误的兜底）
    {
        std::map<std::string, double> sol = {{"a", 0}, {"b", 100}, {"c", 0}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 0.08);
        check("目标值被截断成 0.08 时被拒绝", !r.ok, r.message);
    }

    // 5.4 整数性检查
    {
        std::map<std::string, double> sol = {{"a", 0.5}, {"b", 99.5}, {"c", 0}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 59.7);
        check("非整数解被拒绝", !r.ok, r.message);
    }

    // 5.5 缺变量
    {
        std::map<std::string, double> sol = {{"a", 0}, {"b", 100}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 60.0);
        check("缺少变量取值被拒绝", !r.ok, r.message);
    }

    // 5.6 修复前的 verify() 只会检查长度和 NaN，
    //     用它验同一个不可行解会返回 true —— 这里记录下旧行为的危害
    {
        std::map<std::string, double> sol = {{"a", 0}, {"b", 200}, {"c", 0}};
        check("verify_solution 比长度检查严格",
              !Verifier::verify_solution(vars, ints, cons, obj, sol, 120.0).ok);
    }
}

void test_verifier_minimize_ops() {
    std::cout << "\n[6] >= 与 = 约束的验证\n";

    const std::vector<std::string> vars = {"x", "y"};
    const std::vector<std::string> ints = {"x", "y"};
    const std::vector<std::string> cons = {"3*x + 2*y >= 12", "2*x + 4*y >= 14"};
    const std::string obj = "0.3*x + 0.5*y";

    {
        std::map<std::string, double> sol = {{"x", 3}, {"y", 2}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 1.9);
        check("可行解 (3,2) 通过", r.ok, r.message);
    }
    {
        std::map<std::string, double> sol = {{"x", 1}, {"y", 1}};
        const VerificationReport r =
            Verifier::verify_solution(vars, ints, cons, obj, sol, 0.8);
        check("不可行解 (1,1) 被拒绝", !r.ok, r.message);
    }
    {
        const std::vector<std::string> eq_cons = {"x + y = 10"};
        std::map<std::string, double> sol = {{"x", 4}, {"y", 6}};
        const VerificationReport r = Verifier::verify_solution(
            vars, ints, eq_cons, "x + y", sol, 10.0);
        check("等式约束被满足", r.ok, r.message);

        std::map<std::string, double> bad = {{"x", 4}, {"y", 5}};
        const VerificationReport r2 = Verifier::verify_solution(
            vars, ints, eq_cons, "x + y", bad, 9.0);
        check("等式约束被违反时被拒绝", !r2.ok, r2.message);
    }
}

}  // namespace

int main() {
    std::cout << "====================================================================\n";
    std::cout << "  C++ 侧单元测试：线性表达式解析 + 解验证\n";
    std::cout << "====================================================================\n";

    test_parse_regression();
    test_parse_basic();
    test_parse_rejections();
    test_split_relation();
    test_verifier();
    test_verifier_minimize_ops();

    std::cout << "\n====================================================================\n";
    if (g_failures > 0) {
        std::cout << "  失败 " << g_failures << " / " << g_checks << " 项\n";
        std::cout << "====================================================================\n";
        return 1;
    }
    std::cout << "  全部 " << g_checks << " 项通过\n";
    std::cout << "====================================================================\n";
    return 0;
}
