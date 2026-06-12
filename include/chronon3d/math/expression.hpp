#pragma once

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

/// Carries all context needed for expression evaluation: time, frame, fps,
/// composition dimensions, current layer index, and a cross-layer resolver.
struct ExpressionContext {
    // Time
    double frame{0.0};
    double time{0.0};         // seconds
    double fps{30.0};

    // Composition
    double width{1280.0};
    double height{720.0};
    double num_layers{0.0};

    // Current layer
    double index{0.0};         // 1-based layer index
    double in_point{0.0};      // layer in-point in seconds
    double out_point{0.0};     // layer out-point in seconds
    double width_0{0.0};       // layer source width (pixels)
    double height_0{0.0};      // layer source height (pixels)

    // Current property value (set by AnimatedValue before eval)
    double value{0.0};         // current base value (keyframed)
    double value0{0.0};        // scalar component 0 (same as value for f32)
    double value1{0.0};        // scalar component 1 (for Vec2/Vec3)
    double value2{0.0};        // scalar component 2 (for Vec3)

    /// Cross-layer resolver: given a layer name and a dot-separated property path
    /// (e.g. "transform.opacity"), returns the evaluated value. NaN on failure.
    using LayerPropertyResolver =
        std::function<double(const std::string& layer_name,
                             const std::string& property_path,
                             double time)>;

    LayerPropertyResolver layer_resolver;
};

/// Per-evaluation mutable state (seedRandom, etc.)
struct ExpressionState {
    unsigned int random_seed{0};
    bool seed_random_active{false};

    double posterize_fps{0.0};
    bool posterize_active{false};

    // Cached resolved values for layer("name") lookups within one expression
    // Key: "layer_name::property_path"
    std::unordered_map<std::string, double> layer_cache;
};

namespace expr_detail {

inline double seeded_random(unsigned int seed) {
    // Simple hash-based PRNG — deterministic for a given seed.
    unsigned int x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<double>(x % 100000u) / 100000.0;
}

inline double random_range(double lo, double hi, unsigned int seed) {
    return lo + (hi - lo) * seeded_random(seed);
}

// Wiggle: multi-octave smooth noise, identical to wiggle.hpp but double precision.
inline double wiggle_impl(double freq, double amp, double time, unsigned int seed) {
    if (amp <= 0.0 || freq <= 0.0) return 0.0;

    auto hash_noise = [](int n, unsigned int s) -> double {
        unsigned int x = static_cast<unsigned int>(n) + s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return static_cast<double>(x % 10000u) / 5000.0 - 1.0;
    };

    auto smooth = [&](double t, unsigned int s) -> double {
        const int i = static_cast<int>(std::floor(t));
        const double frac = t - static_cast<double>(i);
        const double u = frac * frac * (3.0 - 2.0 * frac);
        const double a = hash_noise(i, s);
        const double b = hash_noise(i + 1, s);
        return a + (b - a) * u;
    };

    const double n1 = smooth(time * freq, seed);
    const double n2 = smooth(time * freq * 2.0, seed + 1000u) * 0.5;
    const double n3 = smooth(time * freq * 4.0, seed + 2000u) * 0.25;

    constexpr double kNorm = 1.0 / 1.75;
    return (n1 + n2 + n3) * amp * kNorm;
}

// linear(t, t_min, t_max, v_min, v_max) — AE-style remapping.
inline double linear_t(double t, double t_min, double t_max, double v_min, double v_max) {
    if (std::abs(t_max - t_min) < 1e-12) return v_min;
    double frac = (t - t_min) / (t_max - t_min);
    frac = std::clamp(frac, 0.0, 1.0);
    return v_min + frac * (v_max - v_min);
}

// ease(t, t_min, t_max, v_min, v_max) — smoothstep remapping.
inline double ease_t(double t, double t_min, double t_max, double v_min, double v_max) {
    if (std::abs(t_max - t_min) < 1e-12) return v_min;
    double frac = (t - t_min) / (t_max - t_min);
    frac = std::clamp(frac, 0.0, 1.0);
    frac = frac * frac * (3.0 - 2.0 * frac);  // smoothstep
    return v_min + frac * (v_max - v_min);
}

// loopHelper — computes looped time fraction for loopOut/loopIn.
// Returns the fractional position within a loop cycle.
inline double loop_helper(double t, double t_min, double t_max, int num_keyframes,
                          const std::string& type) {
    if (t <= t_min) return t_min;
    if (t_max <= t_min) return t_min;

    const double dur = t_max - t_min;
    double frac = (t - t_min) / dur;

    if (type == "cycle" || type.empty()) {
        // cycle: restart from beginning
        frac = frac - std::floor(frac);
    } else if (type == "pingpong") {
        // pingpong: alternate direction
        double cycle = std::floor(frac);
        frac = frac - cycle;
        if (static_cast<int>(cycle) % 2 == 1) {
            frac = 1.0 - frac;
        }
    } else if (type == "offset") {
        // offset: accumulate value offset per cycle
        frac = frac - std::floor(frac);
    } else {
        // "continue" or unknown: linear extrapolation
        frac = frac - std::floor(frac);
    }

    return t_min + frac * dur;
}

} // namespace expr_detail

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

    double parse_function(std::string name) {
        if (name == "layer") {
            return parse_layer_ref();
        }

        // Parse regular numeric arguments (up to 5)
        double a = 0.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0;
        int nargs = 0;

        skip_ws();
        if (!match(')')) {
            a = parse_ternary();
            nargs = 1;
            skip_ws();
            if (match(',')) { b = parse_ternary(); nargs = 2; skip_ws();
            if (match(',')) { c = parse_ternary(); nargs = 3; skip_ws();
            if (match(',')) { d = parse_ternary(); nargs = 4; skip_ws();
            if (match(',')) { e = parse_ternary(); nargs = 5; skip_ws();
            }}}}
            expect(')');
        }

        if (m_error) return 0.0;

        if (name == "sin")    return std::sin(a);
        if (name == "cos")    return std::cos(a);
        if (name == "tan")    return std::tan(a);
        if (name == "asin")   return std::asin(std::clamp(a, -1.0, 1.0));
        if (name == "acos")   return std::acos(std::clamp(a, -1.0, 1.0));
        if (name == "atan")   return std::atan(a);
        if (name == "atan2")  return std::atan2(a, b);
        if (name == "abs")    return std::fabs(a);
        if (name == "sqrt")   return std::sqrt(std::max(0.0, a));
        if (name == "exp")    return std::exp(a);
        if (name == "log")    return std::log(std::max(1e-300, a));
        if (name == "log10")  return std::log10(std::max(1e-300, a));
        if (name == "ceil")   return std::ceil(a);
        if (name == "floor")  return std::floor(a);
        if (name == "round")  return std::round(a);
        if (name == "trunc")  return std::trunc(a);
        if (name == "sign")   return (a > 0.0) ? 1.0 : ((a < 0.0) ? -1.0 : 0.0);
        if (name == "pow")    return std::pow(a, b);

        if (name == "min")    return std::min(a, b);
        if (name == "max")    return std::max(a, b);
        if (name == "clamp")  return std::clamp(a, b, c);

        if (name == "degreesToRadians" || name == "radians") return a * (3.14159265358979323846 / 180.0);
        if (name == "radiansToDegrees" || name == "degrees") return a * (180.0 / 3.14159265358979323846);

        if (name == "random") {
            unsigned int seed = m_state ? m_state->random_seed : 0;
            if (m_state && m_state->seed_random_active) {
                ++m_state->random_seed;
                seed = m_state->random_seed;
            }
            double r = expr_detail::seeded_random(seed);
            // AE-compatible: random() → [0,1), random(max) → [0,max), random(min,max)
            if (nargs == 0) return r;
            if (nargs == 1) return r * a;
            return a + r * (b - a);
        }

        if (name == "seedRandom") {
            if (m_state) {
                m_state->random_seed = static_cast<unsigned int>(a);
                m_state->seed_random_active = true;
                if (nargs >= 2) {
                    // seedRandom(seed, timeless=false)
                    // When timeless=true, time doesn't affect the seed
                    // We store the seed regardless; "timeless" affects how
                    // subsequent random() calls are made, which we can't fully
                    // model in this contextless double-return.
                }
            }
            return 0.0;
        }

        if (name == "posterizeTime" || name == "posterize") {
            if (m_state) {
                m_state->posterize_fps = std::max(0.001, a);
                m_state->posterize_active = true;
            }
            return a;  // Returns the fps; actual posterization applied externally
        }

        if (name == "wiggle") {
            double freq = std::max(0.001, a);
            double amp  = b;
            unsigned int seed = m_state ? m_state->random_seed : 0;
            double t = m_ctx ? m_ctx->time : 0.0;
            if (nargs >= 3) t = c;  // optional time override
            if (nargs >= 4) seed = static_cast<unsigned int>(d);
            return expr_detail::wiggle_impl(freq, amp, t, seed);
        }

        if (name == "linear") {
            // linear(t, t_min, t_max, v_min, v_max)
            // Also: linear(t, v_min, v_max) — shorthand with t_min=0, t_max=1
            if (nargs == 3) {
                return expr_detail::linear_t(a, 0.0, 1.0, b, c);
            }
            if (nargs >= 5) {
                return expr_detail::linear_t(a, b, c, d, e);
            }
            // nargs == 4: ambiguous — treat as linear(t, t_min, t_max, v_max=1.0, v_min=0.0)
            return expr_detail::linear_t(a, b, c, 0.0, d);
        }

        if (name == "ease") {
            if (nargs == 3) {
                return expr_detail::ease_t(a, 0.0, 1.0, b, c);
            }
            if (nargs >= 5) {
                return expr_detail::ease_t(a, b, c, d, e);
            }
            return expr_detail::ease_t(a, b, c, 0.0, d);
        }

        if (name == "easeIn") {
            // easeIn uses quadratic ease-in instead of smoothstep
            double t_val = a, t_min = 0.0, t_max = 1.0, v_min = b, v_max = c;
            if (nargs >= 5) { t_min = b; t_max = c; v_min = d; v_max = e; }
            if (std::abs(t_max - t_min) < 1e-12) return v_min;
            double frac = std::clamp((t_val - t_min) / (t_max - t_min), 0.0, 1.0);
            frac = frac * frac;  // ease-in
            return v_min + frac * (v_max - v_min);
        }

        if (name == "easeOut") {
            double t_val = a, t_min = 0.0, t_max = 1.0, v_min = b, v_max = c;
            if (nargs >= 5) { t_min = b; t_max = c; v_min = d; v_max = e; }
            if (std::abs(t_max - t_min) < 1e-12) return v_min;
            double frac = std::clamp((t_val - t_min) / (t_max - t_min), 0.0, 1.0);
            frac = 1.0 - (1.0 - frac) * (1.0 - frac);  // ease-out
            return v_min + frac * (v_max - v_min);
        }

        if (name == "loopOut" || name == "loopIn") {
            // loopOut(type, num_keyframes) — simplified: operates on current time range
            std::string type = "cycle";
            int num_keys = 0;

            // First arg could be string or number
            if (nargs >= 1) {
                // If we have context, the user passed a numeric encoding:
                // 0=cycle, 1=pingpong, 2=offset, 3=continue
                int type_idx = static_cast<int>(a);
                switch (type_idx) {
                    case 0: type = "cycle"; break;
                    case 1: type = "pingpong"; break;
                    case 2: type = "offset"; break;
                    case 3: type = "continue"; break;
                    default: type = "cycle"; break;
                }
            }
            if (nargs >= 2) num_keys = static_cast<int>(b);

            // Simplified loopOut: loop the time between inPoint and outPoint
            if (m_ctx) {
                double t_min = m_ctx->in_point;
                double t_max = m_ctx->out_point;
                double t = m_ctx->time;

                if (name == "loopIn") {
                    if (t < t_min) {
                        double looped = expr_detail::loop_helper(t_min + (t_min - t), t_min, t_max, num_keys, type);
                        return m_ctx->value;  // value at looped time (simplified)
                    }
                } else {
                    if (t > t_max) {
                        double looped = expr_detail::loop_helper(t, t_min, t_max, num_keys, type);
                        return m_ctx->value;
                    }
                }
                return m_ctx->value;
            }
            return m_ctx ? m_ctx->value : 0.0;
        }

        if (name == "valueAtTime") {
            // Evaluates this property at a different time.
            // In this implementation, we can only approximate using the context.
            // A full implementation would need to re-evaluate the AnimatedValue
            // at the given time.  For now, return the base value.
            if (m_ctx && m_ctx->layer_resolver) {
                // Attempt to evaluate the current property at a different time
                // by modifying the context time temporarily
                // This is a simplified version — a full implementation would
                // re-evaluate keyframes at the given time.
                return m_ctx->value;
            }
            return m_ctx ? m_ctx->value : 0.0;
        }

        if (name == "toComp" || name == "fromComp") {
            // toComp([x,y,z]) converts from layer space to comp space
            // Returns x component; full 3D return would need Vec3 expression support.
            return a;  // identity transform (no camera offset applied in simplified version)
        }

        m_error = true;
        return 0.0;
    }

    // Returns 0.0 but stores the layer name for postfix member access.
    double parse_layer_ref() {
        skip_ws();
        std::string layer_name;

        // Parse string argument
        if (peek() == '\'' || peek() == '"') {
            char quote = m_expr[m_pos];
            ++m_pos;
            size_t start = m_pos;
            while (m_pos < m_expr.size() && m_expr[m_pos] != quote) ++m_pos;
            layer_name = std::string(m_expr.substr(start, m_pos - start));
            if (m_pos < m_expr.size()) ++m_pos;  // skip closing quote
        } else {
            // Numeric layer index — convert to string for resolver
            double idx = parse_ternary();
            layer_name = "__index__" + std::to_string(static_cast<int>(idx));
        }

        skip_ws();
        expect(')');

        // Store layer name for postfix resolution
        m_current_layer_name = std::move(layer_name);
        m_current_property_path.clear();

        return 0.0;  // actual value resolved by parse_postfix → resolve_member_access
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
