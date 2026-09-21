#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "learning_log.h"
#include "llm_client.h"
#include "model_parser.h"
#include "solver_call.h"
#include "verifier.h"

#define MAX_RETRIES 3

// ---------------------------------------------------------------------------
// Windows 中文编码问题
//
// 窄字符 main(int, char**) 拿到的 argv 是 CRT 按「当前 ANSI 代码页」转换来的。
// 在中文 Windows 上，一长串中文问题会被转成 GBK 字节，而程序随后把这些字节
// 按 UTF-8 编码进 JSON 发给 LLM —— 于是请求体里出现非法 UTF-8，
// 接口直接拒绝：{"code":20015,"message":"The parameter is invalid."}
// （这正是仓库历史日志里第一条失败记录的原因。）
//
// 解决办法：改从宽字符命令行重新取参数并转成 UTF-8，同时把控制台输出
// 代码页设为 UTF-8，保证自己的中文输出也不会乱码。
// ---------------------------------------------------------------------------
static std::vector<std::string> utf8_argv(int argc, char* argv[]) {
#ifdef _WIN32
    int wide_argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv != nullptr) {
        std::vector<std::string> result;
        result.reserve(static_cast<size_t>(wide_argc));
        for (int i = 0; i < wide_argc; ++i) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                                  nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                std::string item(static_cast<size_t>(bytes - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, &item[0], bytes,
                                    nullptr, nullptr);
                result.push_back(item);
            } else {
                result.push_back("");
            }
        }
        LocalFree(wide_argv);
        if (!result.empty()) return result;
    }
#endif
    return std::vector<std::string>(argv, argv + argc);
}

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
            char next = content[j + 1];
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

    // 剥掉 LLM 输出外面的 ```json ... ``` 代码块围栏。
    // 注意 "```json" 是 7 个字符（3 个反引号 + json），原来写的是 b1 + 6，
    // 于是把 'n' 也留在了模型字符串开头 —— 这就是历史日志里每条记录都
    // 出现一行莫名的 "n" 的原因。虽然不影响后续解析，但属于确凿的偏差。
    const size_t b1 = content.find("```json");
    if (b1 != std::string::npos) {
        size_t start = b1 + 7;
        while (start < content.size() &&
               (content[start] == '\n' || content[start] == '\r' ||
                content[start] == ' ')) {
            start++;
        }
        const size_t b2 = content.rfind("```");
        if (b2 != std::string::npos && b2 > start) {
            content = content.substr(start, b2 - start);
        } else {
            content = content.substr(start);
        }
    }
    // 保险起见：如果模型是 ``` 开头（例如没有 json 标注），也剥掉
    const size_t fence = content.find("```");
    if (fence == 0) {
        size_t start = 3;
        while (start < content.size() &&
               (content[start] == '\n' || content[start] == '\r' ||
                content[start] == ' ')) {
            start++;
        }
        const size_t end = content.rfind("```");
        if (end != std::string::npos && end > start) {
            content = content.substr(start, end - start);
        }
    }

    return content;
}

static std::string get_env(const std::string& key,
                           const std::string& fallback = "") {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : fallback;
}

// Windows 上 python3 通常不存在，而 python / py 才是可用的入口
static std::string default_python() {
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
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

**重要约束**：
- 目标函数必须写成"系数*变量"的求和形式，不允许把系数和单位提前乘成一个
  与变量无关的常数。例如三种投资每单位的收益是 0.16、0.6、0.27 万时，
  应写 "0.16*a + 0.6*b + 0.27*c"，而不是 "0.08*2*a + 0.12*5*b + 0.09*3*c"，
  更不允许写成与变量无关的常数。
- 所有变量默认非负且无上界；需要上界时必须显式写出约束。
- 只允许线性表达式与整数/连续变量。

示例：
输入：某工厂生产甲、乙两种产品。甲每件利润3元，乙每件利润5元。设备工时：甲每件用2小时，乙每件用1小时，工时上限12小时。原料：甲每件用3单位，乙每件用1单位，原料上限18单位。求最大利润。
输出：{"v":["x:甲产品数量","y:乙产品数量"],"c":["2*x + 1*y <= 12","3*x + 1*y <= 18"],"o":"3*x + 5*y","t":"maximize"}
)";
}

static std::string get_feedback_prompt(const std::string& previous_error) {
    std::ostringstream oss;
    oss << "上一次建模或求解出错了，错误信息如下：\n" << previous_error
        << "\n\n请检查上面的问题，只输出修正后的 JSON。";
    return oss.str();
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const std::vector<std::string> args = utf8_argv(argc, argv);

    if (args.size() < 2) {
        std::cerr << "用法: ./ad-planner \"你的整数规划问题\" [API_KEY] [PYTHON_PATH]"
                  << std::endl;
        return 1;
    }

    std::string problem = args[1];
    std::string api_key =
        (args.size() > 2) ? args[2] : get_env("SILICONFLOW_API_KEY");
    std::string python_path =
        (args.size() > 3) ? args[3] : default_python();
    std::string script_path = "solver/solve.py";

    std::string api_url = "https://api.siliconflow.cn/v1/chat/completions";
    std::string log_path = get_env("ADPLANNER_LOG", "learning_log.txt");
    const bool logging_enabled = (log_path != "off" && log_path != "OFF");

    std::cout << "=== AdPlanner Agent ===" << std::endl;
    std::cout << "问题: " << problem << std::endl;

    LLMClient llm(api_url, api_key, python_path);
    ModelParser parser;
    SolverCall solver(python_path, script_path);
    LearningLog logger(logging_enabled ? log_path : std::string("learning_log.txt"));

    std::string model_json;
    std::string current_error;
    ParsedModel model;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        std::cout << "\n[尝试 " << attempt << "/" << MAX_RETRIES
                  << "] 调用 LLM..." << std::endl;

        std::string user_msg;
        if (attempt == 1) {
            user_msg = problem;
        } else {
            user_msg = problem + "\n\n" + get_feedback_prompt(current_error);
        }

        LLMResponse resp = llm.chat(get_system_prompt(), user_msg);
        if (!resp.success) {
            std::cerr << "LLM 调用失败: " << resp.error << std::endl;
            current_error = "LLM 调用失败: " + resp.error;
            continue;
        }

        model_json = extract_llm_content(resp.content);

        std::cout << "LLM 返回的模型 JSON: " << model_json << std::endl;

        model = parser.parse(model_json);

        if (model.variables.empty()) {
            current_error = "未能从 LLM 输出中提取到变量（v 字段）";
            std::cerr << current_error << std::endl;
            continue;
        }
        if (model.constraints.empty()) {
            current_error = "未能从 LLM 输出中提取到约束（c 字段）";
            std::cerr << current_error << std::endl;
            continue;
        }
        if (model.objective.empty()) {
            current_error = "未能从 LLM 输出中提取到目标函数（o 字段）";
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
        std::cout << "  目标: " << model.objective << " (" << model.obj_type << ")"
                  << std::endl;

        std::cout << "调用求解器..." << std::endl;
        SolverResult result = solver.solve(model.variables, model.constraints,
                                           model.objective, model.obj_type,
                                           model.integer_vars);

        if (!result.success) {
            // 求解层已经把可读原因放在 error 里（例如"变量 z 未声明"），
            // 直接把这句话回填给 LLM，重试才有意义。
            if (!result.error.empty()) {
                current_error = "求解层返回 " + result.status + "：" + result.error;
            } else if (result.status == "INFEASIBLE") {
                current_error = "模型无解：约束相互矛盾";
            } else if (result.status == "UNBOUNDED") {
                current_error = "目标函数无界：请检查是否遗漏上界约束";
            } else {
                current_error = "求解失败，状态: " + result.status;
            }
            std::cerr << current_error << std::endl;
            continue;
        }

        // 把解向量整理成 变量名 -> 取值
        std::map<std::string, double> assignment;
        for (size_t i = 0; i < result.var_names.size() && i < result.values.size(); i++) {
            assignment[result.var_names[i]] = result.values[i];
        }

        // 真正的验证：代入约束、检查整数性、重算目标值
        VerificationReport report = Verifier::verify_solution(
            model.variables,
            model.integer_vars,
            model.constraints,
            model.objective,
            assignment,
            result.objective_value);

        if (!report.ok) {
            current_error = "解验证失败：" + report.message;
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

        std::cout << "\n=== 独立验证 ===" << std::endl;
        for (const std::string& note : report.notes) {
            std::cout << "  [通过] " << note << std::endl;
        }

        if (logging_enabled) {
            logger.log_attempt(attempt, model_json, problem);
            logger.mark_success(result.objective_value);
            logger.save();
        }
        return 0;
    }

    std::cerr << "\n所有重试均失败。" << std::endl;
    std::cerr << "最后一次错误: " << current_error << std::endl;
    if (logging_enabled) {
        logger.log_attempt(MAX_RETRIES, model_json, problem);
        logger.mark_failure(current_error);
        logger.save();
    }
    return 1;
}
