#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chronon3d::math {

class ExpressionParser {
public:
    ExpressionParser(std::string_view expr,
                     const std::unordered_map<std::string, double>& vars)
        : m_expr(expr), m_vars(vars) {}

    double parse() {
        m_pos = 0;
        double value = parse_expression();
        skip_ws();
        if (m_pos != m_expr.size()) {
            m_error = true;
        }
        return value;
    }

    bool error() const { return m_error; }

private:
    double parse_expression() {
        double lhs = parse_term();
        while (!m_error) {
            skip_ws();
            if (match('+')) {
                lhs += parse_term();
            } else if (match('-')) {
                lhs -= parse_term();
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_term() {
        double lhs = parse_factor();
        while (!m_error) {
            skip_ws();
            if (match('*')) {
                lhs *= parse_factor();
            } else if (match('/')) {
                const double rhs = parse_factor();
                lhs = rhs == 0.0 ? std::numeric_limits<double>::infinity() : lhs / rhs;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_factor() {
        skip_ws();
        if (match('+')) return parse_factor();
        if (match('-')) return -parse_factor();
        if (match('(')) {
            double value = parse_expression();
            expect(')');
            return value;
        }

        if (is_alpha(peek())) {
            std::string ident = parse_identifier();
            skip_ws();
            if (match('(')) {
                return parse_function(std::move(ident));
            }
            auto it = m_vars.find(ident);
            if (it != m_vars.end()) return it->second;
            m_error = true;
            return 0.0;
        }

        return parse_number();
    }

    double parse_function(std::string name) {
        double a = parse_expression();
        double b = 0.0;
        double c = 0.0;
        skip_ws();
        if (match(',')) {
            b = parse_expression();
            skip_ws();
            if (match(',')) {
                c = parse_expression();
            }
        }
        expect(')');

        if (name == "sin") return std::sin(a); // Standard C++: radians
        if (name == "cos") return std::cos(a); // Standard C++: radians
        if (name == "tan") return std::tan(a);
        if (name == "abs") return std::fabs(a);
        if (name == "sqrt") return std::sqrt(std::max(0.0, a));
        if (name == "min") return std::min(a, b);
        if (name == "max") return std::max(a, b);
        if (name == "clamp") return std::clamp(a, b, c);

        m_error = true;
        return 0.0;
    }

    double parse_number() {
        skip_ws();
        const char* begin = m_expr.data() + m_pos;
        const char* end = m_expr.data() + m_expr.size();
        double value = 0.0;
        auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{}) {
            m_error = true;
            return 0.0;
        }
        m_pos = static_cast<size_t>(result.ptr - m_expr.data());
        return value;
    }

    std::string parse_identifier() {
        size_t start = m_pos;
        while (m_pos < m_expr.size() && (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_')) {
            ++m_pos;
        }
        return std::string(m_expr.substr(start, m_pos - start));
    }

    void skip_ws() {
        while (m_pos < m_expr.size() && std::isspace(static_cast<unsigned char>(m_expr[m_pos]))) {
            ++m_pos;
        }
    }

    bool match(char c) {
        skip_ws();
        if (m_pos < m_expr.size() && m_expr[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!match(c)) {
            m_error = true;
        }
    }

    char peek() {
        skip_ws();
        return m_pos < m_expr.size() ? m_expr[m_pos] : '\0';
    }

    static bool is_alpha(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }

    std::string_view m_expr;
    const std::unordered_map<std::string, double>& m_vars;
    size_t m_pos{0};
    bool m_error{false};
};

inline double evaluate_expression(std::string_view expr,
                                  const std::unordered_map<std::string, double>& vars,
                                  double fallback) {
    ExpressionParser parser(expr, vars);
    double value = parser.parse();
    return parser.error() ? fallback : value;
}

} // namespace chronon3d::math
