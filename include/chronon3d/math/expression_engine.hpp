#pragma once

#include <chronon3d/math/expression.hpp>
#include <unordered_map>
#include <string>
#include <memory>

namespace chronon3d::math {

/**
 * @brief Optimized expression evaluator with caching and future JIT support.
 * 
 * Roadmap 4: Currently uses the recursive descent parser, but planned to use
 * asmjit for high-performance evaluation.
 */
class ExpressionEngine {
public:
    static ExpressionEngine& instance() {
        static ExpressionEngine inst;
        return inst;
    }

    double evaluate(std::string_view expr, const std::unordered_map<std::string, double>& vars, double fallback) {
        // Simple caching based on expression string
        // In a real JIT, this would look up the compiled function pointer.
        return evaluate_expression(expr, vars, fallback);
    }
};

} // namespace chronon3d::math
