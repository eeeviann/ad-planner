#include "solver_call.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>

SolverCall::SolverCall(const std::string& python_path, const std::string& script_path)
    : python_path_(python_path), script_path_(script_path) {}

std::string SolverCall::call_python(const std::string& input_json) {

    std::string tmp_in = "/tmp/ip_input.json";
    std::string tmp_out = "/tmp/ip_output.json";

    std::ofstream fin(tmp_in);
    fin << input_json;
    fin.close();


    std::string cmd = python_path_ + " " + script_path_ + " < " + tmp_in + " > " + tmp_out;
    int ret = system(cmd.c_str());

    if (ret != 0) {
        return "{\"status\":\"ERROR\",\"message\":\"Python script failed\"}";
    }

    std::ifstream fout(tmp_out);
    std::stringstream buffer;
    buffer << fout.rdbuf();
    fout.close();
    return buffer.str();
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


    size_t obj_pos = json_str.find("\"objective_value\"");
    if (obj_pos != std::string::npos) {
        size_t colon = json_str.find(':', obj_pos);
        size_t start = colon + 1;
        while (start < json_str.size() && (json_str[start] == ' ' || json_str[start] == '"')) start++;
        size_t end = start;
        while (end < json_str.size() && (isdigit(json_str[end]) || json_str[end] == '.' || json_str[end] == '-' || json_str[end] == 'n' || json_str[end] == 'u')) end++;
        std::string val_str = json_str.substr(start, end - start);
        if (val_str == "null" || val_str == "nan") result.objective_value = 0;
        else result.objective_value = atof(val_str.c_str());
    }


    size_t sol_pos = json_str.find("\"solution\"");
    if (sol_pos != std::string::npos) {
        size_t brace = json_str.find('{', sol_pos);
        int depth = 0;
        size_t end_brace = brace;
        for (size_t i = brace; i < json_str.size(); i++) {
            if (json_str[i] == '{') depth++;
            else if (json_str[i] == '}') { depth--; end_brace = i; if (depth == 0) break; }
        }

        std::string sol_obj = json_str.substr(brace + 1, end_brace - brace - 1);
        size_t pos = 0;
        while (pos < sol_obj.size()) {

            size_t q1 = sol_obj.find('"', pos);
            if (q1 == std::string::npos) break;
            size_t q2 = sol_obj.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string var_name = sol_obj.substr(q1 + 1, q2 - q1 - 1);

            size_t colon = sol_obj.find(':', q2 + 1);
            if (colon == std::string::npos) break;
            size_t val_start = colon + 1;
            while (val_start < sol_obj.size() && (val_start >= sol_obj.size() || (!isdigit(sol_obj[val_start]) && sol_obj[val_start] != '-' && sol_obj[val_start] != '.'))) val_start++;
            size_t val_end = val_start;
            while (val_end < sol_obj.size() && (isdigit(sol_obj[val_end]) || sol_obj[val_end] == '.' || sol_obj[val_end] == '-')) val_end++;

            std::string val_str = sol_obj.substr(val_start, val_end - val_start);
            double val = atof(val_str.c_str());

            result.var_names.push_back(var_name);
            result.values.push_back(val);
            pos = val_end;
        }
    }

    result.success = (result.status == "OPTIMAL" || result.status == "FEASIBLE");
    return result;
}

SolverResult SolverCall::solve(
    const std::vector<std::string>& variables,
    const std::vector<std::string>& constraints,
    const std::string& objective,
    const std::string& obj_type,
    const std::vector<std::string>& integer_vars
) {

    std::ostringstream json;
    json << "{";
    json << "\"variables\":" << "[";
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

    std::string result_json = call_python(json.str());
    return parse_result_json(result_json);
}
