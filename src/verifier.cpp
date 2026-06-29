#include "verifier.h"
#include <cmath>
#include <algorithm>

bool Verifier::verify(
    const std::vector<std::string>& variables,
    const std::vector<std::string>& constraints,
    const std::vector<double>& values,
    double tolerance
) {
    (void)variables;
    (void)constraints;
    if (values.size() != variables.size()) return false;
    for (double v : values) {
        if (std::isnan(v) || std::isinf(v)) return false;
    }
    return true;
}

bool Verifier::verify_integer(
    const std::vector<std::string>& integer_vars,
    const std::vector<std::string>& var_names,
    const std::vector<double>& values,
    double tolerance
) {
    for (const std::string& ivar : integer_vars) {
        for (size_t i = 0; i < var_names.size() && i < values.size(); i++) {
            if (var_names[i] == ivar) {
                double rounded = std::round(values[i]);
                if (std::abs(values[i] - rounded) > tolerance) {
                    return false;
                }
            }
        }
    }
    return true;
}
