#ifndef SOLVER_CALL_H
#define SOLVER_CALL_H

#include <string>
#include <vector>

struct SolverResult {
    bool success;
    std::string status;
    std::vector<std::string> var_names;
    std::vector<double> values;
    double objective_value;
    std::string error;
};

class SolverCall {
public:
    SolverCall(const std::string& python_path, const std::string& script_path);


    SolverResult solve(
        const std::vector<std::string>& variables,
        const std::vector<std::string>& constraints,
        const std::string& objective,
        const std::string& obj_type,
        const std::vector<std::string>& integer_vars
    );

private:
    std::string python_path_;
    std::string script_path_;
    std::string call_python(const std::string& input_json);
    SolverResult parse_result_json(const std::string& json_str);
};

#endif
