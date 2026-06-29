#ifndef VERIFIER_H
#define VERIFIER_H

#include <string>
#include <vector>

class Verifier {
public:
    static bool verify(
        const std::vector<std::string>& variables,
        const std::vector<std::string>& constraints,
        const std::vector<double>& values,
        double tolerance = 1e-6
    );

    static bool verify_integer(
        const std::vector<std::string>& integer_vars,
        const std::vector<std::string>& var_names,
        const std::vector<double>& values,
        double tolerance = 1e-6
    );
};

#endif
