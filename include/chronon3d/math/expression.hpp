#pragma once

// ── Recursive-descent expression parser for AE-style expressions ──────────
//
// This file includes:
//   expression_types.hpp   — ExpressionContext, ExpressionState, expr_detail
//   <this file>             — ExpressionParser class
//
// The public API functions (evaluate_expression) are at the bottom.

#include "expression_types.hpp"

// ── Built-in function dispatch (extracted for readability) ────────────
// Included inline within the ExpressionParser class body.
#include "expression_builtins.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chronon3d::math {

/// Recursive-descent expression parser for AE-style expressions.
class ExpressionParser {
public:
    ExpressionParser(std::string_view expr,
                     const std::unordered_map<std::string, double>& vars,
                     const ExpressionContext* ctx = nullptr,
                     ExpressionState* state = nullptr)
        : m_expr(expr)
        , m_legacy_vars(vars)
        , m_ctx(ctx)
        , m_state(state)
        , m_has_context(ctx != nullptr) {}

    double parse() {
        m_pos = 0;
        double value = parse_ternary();
        skip_ws();
        if (m_pos != m_expr.size()) {
            m_error = true;
        }
        return value;
    }

    bool error() const { return m_error; }

private:
    double parse_ternary() {
        double cond = parse_logical_or();
        skip_ws();
        if (match('?')) {
            double true_val = parse_ternary();  // right-associative
            expect(':');
            double false_val = parse_ternary();
            return cond != 0.0 ? true_val : false_val;
        }
        return cond;
    }

    double parse_logical_or() {
        double lhs = parse_logical_and();
        while (!m_error) {
            skip_ws();
            if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '|' && m_expr[m_pos + 1] == '|') {
                m_pos += 2;
                double rhs = parse_logical_and();
                lhs = (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_logical_and() {
        double lhs = parse_equality();
        while (!m_error) {
            skip_ws();
            if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '&' && m_expr[m_pos + 1] == '&') {
                m_pos += 2;
                double rhs = parse_equality();
                lhs = (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_equality() {
        double lhs = parse_comparison();
        while (!m_error) {
            skip_ws();
            if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '=' && m_expr[m_pos + 1] == '=') {
                m_pos += 2;
                double rhs = parse_comparison();
                lhs = (std::abs(lhs - rhs) < 1e-9) ? 1.0 : 0.0;
            } else if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '!' && m_expr[m_pos + 1] == '=') {
                m_pos += 2;
                double rhs = parse_comparison();
                lhs = (std::abs(lhs - rhs) >= 1e-9) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_comparison() {
        double lhs = parse_additive();
        while (!m_error) {
            skip_ws();
            if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '<' && m_expr[m_pos + 1] == '=') {
                m_pos += 2;
                lhs = (lhs <= parse_additive()) ? 1.0 : 0.0;
            } else if (m_pos + 1 < m_expr.size() && m_expr[m_pos] == '>' && m_expr[m_pos + 1] == '=') {
                m_pos += 2;
                lhs = (lhs >= parse_additive()) ? 1.0 : 0.0;
            } else if (m_pos < m_expr.size() && m_expr[m_pos] == '<') {
                ++m_pos;
                lhs = (lhs < parse_additive()) ? 1.0 : 0.0;
            } else if (m_pos < m_expr.size() && m_expr[m_pos] == '>') {
                ++m_pos;
                lhs = (lhs > parse_additive()) ? 1.0 : 0.0;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_additive() {
        double lhs = parse_multiplicative();
        while (!m_error) {
            skip_ws();
            if (match('+')) {
                lhs += parse_multiplicative();
            } else if (match('-')) {
                lhs -= parse_multiplicative();
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_multiplicative() {
        double lhs = parse_exponentiation();
        while (!m_error) {
            skip_ws();
            if (match('*')) {
                lhs *= parse_exponentiation();
            } else if (match('/')) {
                const double rhs = parse_exponentiation();
                lhs = rhs == 0.0 ? std::numeric_limits<double>::infinity() : lhs / rhs;
            } else if (match('%')) {
                const double rhs = parse_exponentiation();
                lhs = (rhs == 0.0) ? 0.0 : std::fmod(lhs, rhs);
            } else {
                break;
            }
        }
        return lhs;
    }

    double parse_exponentiation() {
        double base = parse_unary();
        skip_ws();
        if (match('^')) {
            double exp = parse_exponentiation();  // right-associative via recursion
            return std::pow(base, exp);
        }
        return base;
    }

    double parse_unary() {
        skip_ws();
        if (match('+')) return parse_unary();
        if (match('-')) return -parse_unary();
        if (match('!')) {
            double val = parse_unary();
            return (val == 0.0) ? 1.0 : 0.0;
        }
        return parse_atom();
    }

    double parse_atom() {
        skip_ws();

        // Parenthesized expression
        if (match('(')) {
            double value = parse_ternary();
            expect(')');
            return parse_postfix(value);
        }

        // String literal — used inside layer('name') calls
        if (peek() == '\'' || peek() == '"') {
            // Unexpected string literal at atom level — store for caller
            m_error = true;
            return 0.0;
        }

        // Identifier or function call
        if (is_alpha(peek())) {
            std::string ident = parse_identifier();
            skip_ws();

            if (match('(')) {
                // Function call
                double val = parse_function(std::move(ident));
                return parse_postfix(val);
            }

            // Variable lookup
            double val = resolve_variable(ident);
            return parse_postfix(val);
        }

        // Number literal
        return parse_number();
    }

    // Postfix: after parsing an atom, check for `.property` or `[index]`.
    // Enables `layer("name").opacity` and `thisComp.width`.
    double parse_postfix(double lhs) {
        while (!m_error) {
            skip_ws();
            if (match('.')) {
                std::string member = parse_identifier();
                skip_ws();
                if (match('(')) {
                    // method call on the "object" — e.g. layer("bg").toComp([0,0,0])
                    // For now, treat as unknown (return 0)
                    // Skip arguments
                    int depth = 1;
                    while (m_pos < m_expr.size() && depth > 0) {
                        if (m_expr[m_pos] == '(') ++depth;
                        if (m_expr[m_pos] == ')') --depth;
                        ++m_pos;
                    }
                    lhs = 0.0;  // method calls return 0 for now
                } else {
                    // property access — resolve via m_current_object
                    lhs = resolve_member_access(lhs, member);
                }
            } else if (match('[')) {
                // Bracket access — e.g. position[0], value[1]
                double idx = parse_ternary();
                expect(']');
                lhs = resolve_bracket_access(lhs, idx);
            } else {
                break;
            }
        }
        return lhs;
    }

    double resolve_variable(const std::string& ident) {
        if (m_has_context) {
            // AE-standard variables
            if (ident == "time")          return m_ctx->time;
            if (ident == "frame")         return m_ctx->frame;
            if (ident == "fps")           return m_ctx->fps;
            if (ident == "index")         return m_ctx->index;
            if (ident == "value")         return m_ctx->value;
            if (ident == "value0")        return m_ctx->value0;
            if (ident == "value1")        return m_ctx->value1;
            if (ident == "value2")        return m_ctx->value2;
            if (ident == "width")         return m_ctx->width;
            if (ident == "height")        return m_ctx->height;
            if (ident == "numLayers")     return m_ctx->num_layers;
            if (ident == "inPoint")       return m_ctx->in_point;
            if (ident == "outPoint")      return m_ctx->out_point;

            // Composition pseudo-object — mark for postfix resolution
            if (ident == "thisComp") {
                m_current_object = "thisComp";
                return 0.0;  // will be resolved by parse_postfix
            }
            if (ident == "thisLayer") {
                m_current_object = "thisLayer";
                return 0.0;
            }
            if (ident == "thisProperty") {
                m_current_object = "thisProperty";
                return 0.0;
            }

            // True/false
            if (ident == "true")  return 1.0;
            if (ident == "false") return 0.0;
            if (ident == "PI")    return 3.14159265358979323846;
            if (ident == "E")     return 2.71828182845904523536;

            // Fallback to legacy vars
            auto it = m_legacy_vars.find(ident);
            if (it != m_legacy_vars.end()) return it->second;

            m_error = true;
            return 0.0;
        }

        // Legacy path — also recognize constants
        if (ident == "true")  return 1.0;
        if (ident == "false") return 0.0;
        if (ident == "PI")    return 3.14159265358979323846;
        if (ident == "E")     return 2.71828182845904523536;

        auto it = m_legacy_vars.find(ident);
        if (it != m_legacy_vars.end()) return it->second;
        m_error = true;
        return 0.0;
    }

    double resolve_member_access(double lhs, const std::string& member) {
        // thisComp.width / thisComp.height / thisComp.numLayers
        if (m_current_object == "thisComp") {
            m_current_object.clear();
            if (member == "width")     return m_ctx->width;
            if (member == "height")    return m_ctx->height;
            if (member == "numLayers") return m_ctx->num_layers;
            if (member == "name")      return 0.0;  // string, can't represent
            return 0.0;
        }

        if (m_current_object == "thisLayer") {
            m_current_object.clear();
            if (member == "index")    return m_ctx->index;
            if (member == "name")     return 0.0;   // string
            if (member == "inPoint")  return m_ctx->in_point;
            if (member == "outPoint") return m_ctx->out_point;
            if (member == "width")    return m_ctx->width_0;
            if (member == "height")   return m_ctx->height_0;
            // thisLayer.opacity, .position, etc. — resolve from current value
            if (member == "opacity")  return m_ctx->value;
            return 0.0;
        }

        if (m_current_object == "thisProperty") {
            m_current_object.clear();
            if (member == "value")   return m_ctx->value;
            if (member == "name")    return 0.0;  // string
            if (member == "index")   return 0.0;
            if (member == "value0")  return m_ctx->value0;
            if (member == "value1")  return m_ctx->value1;
            if (member == "value2")  return m_ctx->value2;
            return 0.0;
        }

        // layer("name") result — lhs was returned by layer() which sets m_current_layer_name
        if (!m_current_layer_name.empty()) {
            // Build property path: e.g. "transform.position" → member is "position"
            std::string prop = member;
            // Consume more dot-access to build full path (e.g. transform.position.x)
            // m_current_property_path accumulates depth
            if (!m_current_property_path.empty()) {
                prop = m_current_property_path + "." + member;
            }

            // Check for sub-properties that need more dots
            // Common patterns: transform.position, transform.opacity, position.x
            skip_ws();
            if (m_pos < m_expr.size() && m_expr[m_pos] == '.') {
                // Peek ahead to see if next token is a leaf property
                size_t saved = m_pos;
                ++m_pos;  // skip '.'
                std::string next = parse_identifier();

                // Leaf properties (scalar)
                bool is_leaf = (next == "x" || next == "y" || next == "z" ||
                                next == "w" || next == "h" ||
                                next == "0" || next == "1" || next == "2" ||
                                next == "value");

                if (is_leaf) {
                    // This is a terminal access — resolve the full path
                    std::string full_path = prop + "." + next;
                    return resolve_layer_property(full_path);
                } else {
                    // Intermediate dot — restore position so parse_postfix
                    // re-processes the '.next' part on the next loop iteration.
                    // Set accumulated path so the next resolve_member_access
                    // call picks it up.
                    m_pos = saved;
                    m_current_property_path = prop;
                    return 0.0;
                }
            }

            // Terminal access — resolve
            // (m_current_layer_name and m_current_property_path are
            //  cleared inside resolve_layer_property)
            return resolve_layer_property(prop);
        }

        // Default: unknown member access
        return lhs;
    }

    double resolve_bracket_access(double lhs, double idx) {
        // For Vec3 values: [0]=x, [1]=y, [2]=z
        // If we're accessing a layer property, include the index in the path
        if (!m_current_layer_name.empty()) {
            std::string prop = m_current_property_path;
            if (!prop.empty()) prop += ".";
            prop += std::to_string(static_cast<int>(idx));
            return resolve_layer_property(prop);
        }

        // thisProperty[0], thisProperty[1], etc.
        if (m_current_object == "thisProperty") {
            int i = static_cast<int>(idx);
            if (i == 0) return m_ctx->value0;
            if (i == 1) return m_ctx->value1;
            if (i == 2) return m_ctx->value2;
            return 0.0;
        }

        return lhs;
    }

    double resolve_layer_property(const std::string& prop_path) {
        if (!m_ctx || !m_ctx->layer_resolver) return 0.0;

        // Check cache first
        std::string cache_key = m_current_layer_name + "::" + prop_path;
        if (m_state) {
            auto it = m_state->layer_cache.find(cache_key);
            if (it != m_state->layer_cache.end()) return it->second;
        }

        double result = m_ctx->layer_resolver(m_current_layer_name, prop_path, m_ctx->time);

        if (m_state && !std::isnan(result)) {
            m_state->layer_cache[cache_key] = result;
        }

        m_current_layer_name.clear();
        m_current_property_path.clear();
        return result;
    }

    // parse_function() + parse_layer_ref() — included inline below.
    #include "expression_builtins.hpp"

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
        while (m_pos < m_expr.size() &&
               (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_')) {
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
    const std::unordered_map<std::string, double>& m_legacy_vars;
    const ExpressionContext* m_ctx;
    ExpressionState* m_state;
    bool m_has_context;

    size_t m_pos{0};
    bool m_error{false};

    // Dot-notation tracking
    std::string m_current_object;        // "thisComp", "thisLayer", "__layer__", etc.
    std::string m_current_layer_name;    // Set by layer("name") calls
    std::string m_current_property_path; // Accumulated property path for nested dot access
};

/// Legacy API — backward compatible.
inline double evaluate_expression(std::string_view expr,
                                  const std::unordered_map<std::string, double>& vars,
                                  double fallback) {
    ExpressionParser parser(expr, vars);
    double value = parser.parse();
    return parser.error() ? fallback : value;
}

/// Context-aware API — supports cross-layer references, AE-standard variables.
inline double evaluate_expression(std::string_view expr,
                                  const ExpressionContext& ctx,
                                  const std::unordered_map<std::string, double>& extra_vars,
                                  double fallback) {
    ExpressionState state;
    ExpressionParser parser(expr, extra_vars, &ctx, &state);
    double value = parser.parse();
    return parser.error() ? fallback : value;
}

/// Context-aware API with external state (for seedRandom persistence within an expression).
inline double evaluate_expression(std::string_view expr,
                                  const ExpressionContext& ctx,
                                  const std::unordered_map<std::string, double>& extra_vars,
                                  ExpressionState& state,
                                  double fallback) {
    ExpressionParser parser(expr, extra_vars, &ctx, &state);
    double value = parser.parse();
    return parser.error() ? fallback : value;
}

} // namespace chronon3d::math
