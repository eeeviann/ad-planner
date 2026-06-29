#include "model_parser.h"
#include <algorithm>
#include <cctype>
#include <iostream>




static std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && std::isspace(s[start])) start++;
    while (end > start && std::isspace(s[end-1])) end--;
    return s.substr(start, end - start);
}

static std::string extract_quoted_string(const std::string& json, size_t start) {

    size_t i = start + 1;
    std::string result;
    while (i < json.size()) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char escaped = json[i+1];
            if (escaped == 'n') result += '\n';
            else if (escaped == 't') result += '\t';
            else if (escaped == '"') result += '"';
            else if (escaped == '\\') result += '\\';
            else result += escaped;
            i += 2;
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i++];
        }
    }
    return result;
}

static std::vector<std::string> extract_array(const std::string& json, size_t arr_start) {
    std::vector<std::string> result;
    size_t i = arr_start + 1;
    while (i < json.size()) {
        while (i < json.size() && std::isspace(json[i])) i++;
        if (i >= json.size() || json[i] == ']') { i++; break; }
        if (json[i] == '"') {
            std::string val = extract_quoted_string(json, i);
            result.push_back(val);
            i += val.size() + 2;
            while (i < json.size() && json[i] != ',' && json[i] != ']') i++;
            if (json[i] == ',') i++;
        } else {

            size_t depth = 0;
            std::string token;
            while (i < json.size()) {
                if (json[i] == '{' || json[i] == '[') depth++;
                else if (json[i] == '}' || json[i] == ']') depth--;
                else if (json[i] == ',' && depth == 0) break;
                token += json[i++];
            }
            if (!trim(token).empty()) result.push_back(trim(token));
            if (json[i] == ',') i++;
        }
        while (i < json.size() && std::isspace(json[i])) i++;
    }
    return result;
}

static std::string find_key_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return "";

    size_t value_start = colon + 1;
    while (value_start < json.size() && std::isspace(json[value_start])) value_start++;
    if (value_start >= json.size()) return "";

    if (json[value_start] == '"') {
        return extract_quoted_string(json, value_start);
    } else if (json[value_start] == '[') {
        return "";
    } else {

        size_t end = value_start;
        while (end < json.size() && json[end] != ',' && json[end] != '}') end++;
        return trim(json.substr(value_start, end - value_start));
    }
}

static std::vector<std::string> find_array(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};

    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return {};

    size_t arr_start = colon + 1;
    while (arr_start < json.size() && std::isspace(json[arr_start])) arr_start++;
    if (arr_start >= json.size() || json[arr_start] != '[') return {};

    return extract_array(json, arr_start);
}


static std::pair<std::string, std::string> parse_var_with_desc(const std::string& item) {
    size_t colon = item.find(':');
    if (colon != std::string::npos) {
        return {trim(item.substr(0, colon)), trim(item.substr(colon+1))};
    }
    return {trim(item), ""};
}

ParsedModel ModelParser::parse(const std::string& json_str) {
    ParsedModel model;


    std::string t = find_key_value(json_str, "t");
    if (t.empty()) t = find_key_value(json_str, "type");
    if (t.empty()) t = "maximize";
    model.obj_type = t;


    model.objective = find_key_value(json_str, "o");
    if (model.objective.empty()) {
        model.objective = find_key_value(json_str, "objective");
    }


    std::vector<std::string> c_arr = find_array(json_str, "c");
    if (c_arr.empty()) c_arr = find_array(json_str, "constraints");
    for (const auto& c : c_arr) {
        model.constraints.push_back(c);
    }


    std::vector<std::string> v_arr = find_array(json_str, "v");
    if (v_arr.empty()) v_arr = find_array(json_str, "variables");
    for (const auto& item : v_arr) {
        auto [var_name, desc] = parse_var_with_desc(item);
        if (!var_name.empty()) {
            model.variables.push_back(var_name);
            if (!desc.empty()) model.var_names.push_back(desc);
        }
    }


    std::vector<std::string> int_arr = find_array(json_str, "integer_vars");
    if (!int_arr.empty()) {
        for (const auto& item : int_arr) {
            model.integer_vars.push_back(trim(item));
        }
    } else {
        model.integer_vars = model.variables;
    }

    return model;
}
