#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cstdlib>
#include "llm_client.h"
#include "model_parser.h"
#include "solver_call.h"
#include "verifier.h"
#include "learning_log.h"

#define MAX_RETRIES 3


static std::string extract_llm_content(const std::string& json) {

    size_t choices_pos = json.find("\"choices\"");
    if (choices_pos == std::string::npos) return json;

    size_t msg_pos = json.find("\"message\"", choices_pos);
    if (msg_pos == std::string::npos) return json;

    size_t content_pos = json.find("\"content\"", msg_pos);
    if (content_pos == std::string::npos) return json;

    size_t q1 = json.find('"', content_pos + 9);
    if (q1 == std::string::npos) return json;
    q1++;


    size_t i = q1;
    while (i < json.size()) {
        if (json[i] == '\\') { i += 2; continue; }
        if (json[i] == '"') break;
        i++;
    }

    std::string content = json.substr(q1, i - q1);


    std::string unescaped;
    for (size_t j = 0; j < content.size(); j++) {
        if (content[j] == '\\' && j + 1 < content.size()) {
            char next = content[j+1];
            if (next == 'n') { unescaped += '\n'; j++; }
            else if (next == 't') { unescaped += '\t'; j++; }
            else if (next == '"') { unescaped += '"'; j++; }
            else if (next == '\\') { unescaped += '\\'; j++; }
            else { unescaped += content[j]; }
        } else {
            unescaped += content[j];
        }
    }
    content = unescaped;


    size_t pos = 0;
    while ((pos = content.find("\\u003c", pos)) != std::string::npos) {
        content.replace(pos, 6, "<");
    }
    pos = 0;
    while ((pos = content.find("\\u003e", pos)) != std::string::npos) {
        content.replace(pos, 6, ">");
    }


    size_t b1 = content.find("```json");
    if (b1 != std::string::npos) {
        size_t start = b1 + 6;
        while (start < content.size() && (content[start] == '\n' || content[start] == ' ')) start++;
        size_t b2 = content.rfind("```");
        if (b2 != std::string::npos && b2 > start) {
            content = content.substr(start, b2 - start);
        }
    }

    return content;
}

static std::string get_env(const std::string& key, const std::string& fallback = "") {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : fallback;
}

static std::string get_system_prompt() {
    return R"(你是一个整数规划建模助手。

用户会给你一个实际问题，你需要提取其中的：
1. 决策变量（variables）
2. 约束条件（constraints） 
3. 目标函数（objective）
4. 目标类型（type: maximize 或 minimize）

**输出格式**：只输出 JSON，不要其他文字。
JSON 必须包含以下字段：
- v: 变量列表，格式 "变量名:中文描述"，如 ["x:甲产品数量", "y:乙产品数量"]
- c: 约束列表，每个约束写成标准数学表达式，如 ["2*x + 1*y <= 12", "3*x + 1*y <= 18"]
- o: 目标函数表达式，如 "3*x + 5*y"
- t: 目标类型，"maximize" 或 "minimize"

示例：
输入：某工厂生产甲、乙两种产品。甲每件利润3元，乙每件利润5元。设备工时：甲每件用2小时，乙每件用1小时，工时上限12小时。原料：甲每件用3单位，乙每件用1单位，原料上限18单位。求最大利润。
输出：{"v":["x:甲产品数量","y:乙产品数量"],"c":["2*x + 1*y <= 12","3*x + 1*y <= 18"],"o":"3*x + 5*y","t":"maximize"}
)";
}

static std::string get_feedback_prompt(const std::string& previous_error) {
    std::ostringstream oss;
    oss << "上一次建模出错了：\n" << previous_error << "\n\n请重新分析问题，输出正确的 JSON。";
    return oss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: ./ad-planner \"你的整数规划问题\" [API_KEY] [PYTHON_PATH]" << std::endl;
        return 1;
    }

    std::string problem = argv[1];
    std::string api_key = (argc > 2) ? argv[2] : get_env("SILICONFLOW_API_KEY");
    std::string python_path = (argc > 3) ? argv[3] : "python3";
    std::string script_path = "solver/solve.py";


    std::string api_url = "https://api.siliconflow.cn/v1/chat/completions";
    std::string log_path = "learning_log.txt";

    std::cout << "=== AdPlanner Agent ===" << std::endl;
    std::cout << "问题: " << problem << std::endl;


    LLMClient llm(api_url, api_key);
    ModelParser parser;
    SolverCall solver(python_path, script_path);
    LearningLog logger(log_path);

    std::string llm_response_json;
    std::string model_json;
    std::string current_error;
    ParsedModel model;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        std::cout << "\n[尝试 " << attempt << "/" << MAX_RETRIES << "] 调用 LLM..." << std::endl;

        std::string user_msg;
        if (attempt == 1) {
            user_msg = problem;
        } else {
            user_msg = problem + "\n\n" + get_feedback_prompt(current_error);
        }

        LLMResponse resp = llm.chat(get_system_prompt(), user_msg);
        if (!resp.success) {
            std::cerr << "LLM 调用失败: " << resp.error << std::endl;
            current_error = "LLM HTTP 错误: " + resp.error;
            continue;
        }

        llm_response_json = resp.content;
        model_json = extract_llm_content(resp.content);

        std::cout << "LLM 原始输出: " << model_json << std::endl;


        model = parser.parse(model_json);


        if (model.variables.empty()) {
            current_error = "未能提取到变量";
            std::cerr << current_error << std::endl;
            continue;
        }
        if (model.constraints.empty()) {
            current_error = "未能提取到约束";
            std::cerr << current_error << std::endl;
            continue;
        }
        if (model.objective.empty()) {
            current_error = "未能提取到目标函数";
            std::cerr << current_error << std::endl;
            continue;
        }

        std::cout << "模型提取成功:" << std::endl;
        std::cout << "  变量: ";
        for (size_t i = 0; i < model.variables.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << model.variables[i];
            if (i < model.var_names.size()) std::cout << "(" << model.var_names[i] << ")";
        }
        std::cout << std::endl;
        std::cout << "  约束数: " << model.constraints.size() << std::endl;
        std::cout << "  目标: " << model.objective << " (" << model.obj_type << ")" << std::endl;


        std::cout << "调用求解器..." << std::endl;
        SolverResult result = solver.solve(
            model.variables,
            model.constraints,
            model.objective,
            model.obj_type,
            model.integer_vars
        );

        if (!result.success) {
            if (result.status == "INFEASIBLE") {
                current_error = "模型无解（约束矛盾）";
                std::cerr << current_error << ": " << result.status << std::endl;
            } else {
                current_error = "求解失败: " + result.status;
                std::cerr << current_error << std::endl;
            }
            continue;
        }


        if (!Verifier::verify(model.variables, model.constraints, result.values)) {
            current_error = "解验证失败";
            std::cerr << current_error << std::endl;
            continue;
        }


        std::cout << "\n=== 解算成功 ===" << std::endl;
        std::cout << "状态: " << result.status << std::endl;
        std::cout << "最优值: " << result.objective_value << std::endl;
        std::cout << "最优解:" << std::endl;
        for (size_t i = 0; i < result.var_names.size() && i < result.values.size(); i++) {
            std::string desc = (i < model.var_names.size()) ? model.var_names[i] : "";
            std::cout << "  " << result.var_names[i];
            if (!desc.empty()) std::cout << " (" << desc << ")";
            std::cout << " = " << result.values[i] << std::endl;
        }


        logger.log_attempt(attempt, model_json, model_json, problem);
        logger.mark_success(result.objective_value);
        logger.save();
        return 0;
    }


    std::cerr << "\n所有重试均失败，记录日志。" << std::endl;
    logger.log_attempt(MAX_RETRIES, llm_response_json, model_json, problem);
    logger.mark_failure(current_error);
    logger.save();
    return 1;
}
