#ifndef MODEL_PARSER_H
#define MODEL_PARSER_H

#include <string>
#include <vector>

struct ParsedModel {
    std::vector<std::string> variables;
    std::vector<std::string> var_names;
    std::vector<std::string> constraints;
    std::string objective;
    std::string obj_type;
    std::vector<std::string> integer_vars;
    std::string error;
};

class ModelParser {
public:

    ParsedModel parse(const std::string& json_str);

private:
    std::string extract_string(const std::string& json, const std::string& key);
    std::vector<std::string> extract_array(const std::string& json, const std::string& key);
    std::string between(const std::string& s, const std::string& before, const std::string& after);
};

#endif
