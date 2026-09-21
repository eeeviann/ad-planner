#ifndef VERIFIER_H
#define VERIFIER_H

#include <map>
#include <string>
#include <vector>

// 验证报告。
//
// 修复前：Verifier::verify() 只是检查"解向量长度对不对、有没有 NaN/Inf"，
// 完全没有代入约束，等于什么都没验，但 README 却宣称"求解结果验证"。
// 修复后：本结构记录真实的验证过程与结论，失败原因会回填给 LLM 重试。
struct VerificationReport {
    bool ok = false;
    std::string message = "未执行验证";

    // 重算出的目标函数值（用解向量代入 LLM 给出的目标表达式）
    double recomputed_objective = 0.0;
    bool objective_checked = false;   // 目标值是否比对通过

    // 违约约束的序号（从 1 开始），-1 表示没有违约
    int violated_constraint_index = -1;
    std::string violated_constraint;

    // 逐条人类可读的说明，失败时可直接作为反馈喂给 LLM
    std::vector<std::string> notes;
};

class Verifier {
public:
    // 完整验证链：
    //   1. 结构检查：变量齐全、取值有限
    //   2. 整数性检查：integer_vars 的取值必须是整数
    //   3. 约束检查：把解代入每一条约束，逐条判断是否满足
    //   4. 目标值检查：用解重算目标函数，与求解器报告的值比对
    static VerificationReport verify_solution(
        const std::vector<std::string>& variables,
        const std::vector<std::string>& integer_vars,
        const std::vector<std::string>& constraints,
        const std::string& objective,
        const std::map<std::string, double>& assignment,
        double reported_objective,
        double tolerance = 1e-6);

    // 单独检查整数性，失败时把原因写入 error
    static bool verify_integer(
        const std::vector<std::string>& integer_vars,
        const std::map<std::string, double>& assignment,
        double tolerance,
        std::string& error);
};

#endif  // VERIFIER_H
