#include "solver_call.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "shell_command.h"

namespace {

// 修复前这里写死了 "/tmp/ip_input.json"。Windows 上没有 /tmp，
// 求解器直接失败，日志里那条 "求解失败: ERROR" 就是这么来的。
std::string temp_dir() {
    const char* keys[] = {"TMPDIR", "TEMP", "TMP"};
    for (const char* key : keys) {
        const char* value = std::getenv(key);
        if (value != nullptr && *value != '\0') return std::string(value);
    }
    return ".";
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") return name;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

// 用双引号包住路径，cmd.exe 与 sh 都认
std::string quote(const std::string& s) { return shell_quote(s); }

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 把任意文本安全地塞进 JSON 字符串字面量
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string json_error(const std::string& message) {
    return "{\"status\":\"ERROR\",\"message\":\"" + json_escape(message) + "\"}";
}

}  // namespace

SolverCall::SolverCall(const std::string& python_path,
                       const std::string& script_path)
    : python_path_(python_path), script_path_(script_path) {}

std::string SolverCall::call_python(const std::string& input_json) {
    const std::string dir = temp_dir();
    const std::string tmp_in = join_path(dir, "adplanner_input.json");
    const std::string tmp_out = join_path(dir, "adplanner_output.json");
    const std::string tmp_err = join_path(dir, "adplanner_output.err");

    {
        std::ofstream fin(tmp_in, std::ios::binary);
        if (!fin) {
            return "{\"status\":\"ERROR\",\"message\":\"无法写入临时输入文件\"}";
        }
        fin << input_json;
    }

    const std::string cmd = for_shell(
        quote(python_path_) + " " + quote(script_path_) + " < " +
        quote(tmp_in) + " > " + quote(tmp_out) + " 2> " + quote(tmp_err));
    const int ret = std::system(cmd.c_str());

    std::string output = read_file(tmp_out);

    if (ret != 0) {
        const std::string err = trim(read_file(tmp_err));
        std::string message = "求解脚本退出码 " + std::to_string(ret);
        if (!err.empty()) message += "；stderr: " + err;
        return json_error(message);
    }

    if (trim(output).empty()) {
        const std::string err = trim(read_file(tmp_err));
        std::string message = "求解脚本没有产生输出";
        if (!err.empty()) message += "；stderr: " + err;
        return json_error(message);
    }

    return output;
}

SolverResult SolverCall::parse_result_json(const std::string& json_str) {
    SolverResult result = {false, "", {}, {}, 0.0, ""};

    size_t status_pos = json_str.find("\"status\"");
    if (status_pos != std::string::npos) {
        size_t colon = json_str.find(':', status_pos);
        size_t start = json_str.find('"', colon + 1);
        size_t end = json_str.find('"', start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            result.status = json_str.substr(start + 1, end - start - 1);
        }
    }

    // 把求解层给出的可读错误原因取出来，供主循环反馈给 LLM
    size_t msg_pos = json_str.find("\"message\"");
    if (msg_pos != std::string::npos) {
        size_t colon = json_str.find(':', msg_pos);
        size_t start = json_str.find('"', colon + 1);
        if (start != std::string::npos) {
            std::string value;
            size_t i = start + 1;
            while (i < json_str.size() && json_str[i] != '"') {
                if (json_str[i] == '\\' && i + 1 < json_str.size()) {
                    char next = json_str[i + 1];
                    if (next == 'n') { value += '\n'; i += 2; continue; }
                    if (next == '"') { value += '"'; i += 2; continue; }
                    if (next == '\\') { value += '\\'; i += 2; continue; }
                }
                value += json_str[i++];
            }
            result.error = value;
        }
    }

    size_t obj_pos = json_str.find("\"objective_value\"");
    if (obj_pos != std::string::npos) {
        size_t colon = json_str.find(':', obj_pos);
        size_t start = colon + 1;
        while (start < json_str.size() &&
               (json_str[start] == ' ' || json_str[start] == '"')) {
            start++;
        }
        size_t end = start;
        while (end < json_str.size() &&
               (isdigit(static_cast<unsigned char>(json_str[end])) ||
                json_str[end] == '.' || json_str[end] == '-' ||
                json_str[end] == 'e' || json_str[end] == 'E' ||
                json_str[end] == '+')) {
            end++;
        }
        const std::string val_str = json_str.substr(start, end - start);
        result.objective_value = val_str.empty() ? 0.0 : atof(val_str.c_str());
    }

    size_t sol_pos = json_str.find("\"solution\"");
    if (sol_pos != std::string::npos) {
        size_t brace = json_str.find('{', sol_pos);
        if (brace != std::string::npos) {
            int depth = 0;
            size_t end_brace = brace;
            for (size_t i = brace; i < json_str.size(); i++) {
                if (json_str[i] == '{') {
                    depth++;
                } else if (json_str[i] == '}') {
                    depth--;
                    end_brace = i;
                    if (depth == 0) break;
                }
            }

            const std::string sol_obj =
                json_str.substr(brace + 1, end_brace - brace - 1);
            size_t pos = 0;
            while (pos < sol_obj.size()) {
                size_t q1 = sol_obj.find('"', pos);
                if (q1 == std::string::npos) break;
                size_t q2 = sol_obj.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                const std::string var_name = sol_obj.substr(q1 + 1, q2 - q1 - 1);

                size_t colon = sol_obj.find(':', q2 + 1);
                if (colon == std::string::npos) break;

                size_t val_start = colon + 1;
                while (val_start < sol_obj.size() &&
                       !isdigit(static_cast<unsigned char>(sol_obj[val_start])) &&
                       sol_obj[val_start] != '-' && sol_obj[val_start] != '.' &&
                       sol_obj[val_start] != 'n' && sol_obj[val_start] != 'N') {
                    val_start++;
                }
                size_t val_end = val_start;
                while (val_end < sol_obj.size() &&
                       (isdigit(static_cast<unsigned char>(sol_obj[val_end])) ||
                        sol_obj[val_end] == '.' || sol_obj[val_end] == '-' ||
                        sol_obj[val_end] == 'e' || sol_obj[val_end] == 'E' ||
                        sol_obj[val_end] == '+' || sol_obj[val_end] == 'n' ||
                        sol_obj[val_end] == 'N')) {
                    val_end++;
                }

                const std::string val_str =
                    sol_obj.substr(val_start, val_end - val_start);
                result.var_names.push_back(var_name);
                result.values.push_back(atof(val_str.c_str()));
                pos = val_end;
            }
        }
    }

    result.success = (result.status == "OPTIMAL" || result.status == "FEASIBLE");
    return result;
}

SolverResult SolverCall::solve(const std::vector<std::string>& variables,
                               const std::vector<std::string>& constraints,
                               const std::string& objective,
                               const std::string& obj_type,
                               const std::vector<std::string>& integer_vars) {
    std::ostringstream json;
    json << "{";
    json << "\"variables\":[";
    for (size_t i = 0; i < variables.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << variables[i] << "\"";
    }
    json << "],";

    json << "\"constraints\":[";
    for (size_t i = 0; i < constraints.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << constraints[i] << "\"";
    }
    json << "],";

    json << "\"objective\":\"" << objective << "\",";
    json << "\"type\":\"" << obj_type << "\",";
    json << "\"integer_vars\":[";
    for (size_t i = 0; i < integer_vars.size(); i++) {
        if (i > 0) json << ",";
        json << "\"" << integer_vars[i] << "\"";
    }
    json << "]}";

    const std::string result_json = call_python(json.str());
    return parse_result_json(result_json);
}
