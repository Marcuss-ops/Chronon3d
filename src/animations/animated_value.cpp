#include <chronon3d/animation/core/animated_value.hpp>

#include <chronon3d/math/expression.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace chronon3d {

namespace detail {

namespace {

/// Stack-only splitter for exactly four comma-separated `solid(...)` arguments.
/// Zero heap traffic: the four slices point into the caller's expression.
[[nodiscard]] bool split_solid_4_args(std::string_view inner,
                                     std::array<std::string_view, 4>& out) noexcept {
    int depth = 0;
    std::size_t start = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        } else if (c == ',' && depth == 0) {
            if (count == 4) return false;
            out[count++] = inner.substr(start, i - start);
            start = i + 1;
        }
    }
    // Fewer than three separators, or a trailing empty slot, is not `solid(...)`.
    if (count != 3 || start >= inner.size()) return false;
    out[3] = inner.substr(start);
    return true;
}

/// Whole-argument decimal literal (the overwhelming case: `solid(0.1, ...)`).
/// Returns false for anything the parser must see (variables, operators, hex,
/// out-of-range values), so the fast path can never change the result.
[[nodiscard]] bool parse_literal_number(std::string_view arg, double& out) noexcept {
    while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
        arg.remove_prefix(1);
    while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
        arg.remove_suffix(1);
    if (arg.empty()) return false;
    const char* first = arg.data();
    const char* last = first + arg.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last && std::isfinite(out);
}

struct SolidColorVars {
    std::unordered_map<std::string, double> vars;

    SolidColorVars() {
        vars.reserve(4);
        vars.emplace("frame", 0.0);
        vars.emplace("time", 0.0);
        vars.emplace("fps", 0.0);
        vars.emplace("index", 0.0);
    }
};

/// Per-thread variable table for `solid(...)` expressions: the four canonical
/// keys are allocated once per thread and only their values are updated, so a
/// steady-state frame performs zero heap allocations for the map.
[[nodiscard]] const std::unordered_map<std::string, double>& solid_color_vars(
    double frame, double t, double fps, double index) {
    static thread_local SolidColorVars tls;
    tls.vars["frame"] = frame;
    tls.vars["time"] = t;
    tls.vars["fps"] = fps;
    tls.vars["index"] = index;
    return tls.vars;
}

} // namespace

std::vector<std::string> split_expr_args(const std::string& inner) {
    std::vector<std::string> args;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        char c = inner[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) {
            args.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < inner.size()) {
        args.push_back(inner.substr(start));
    }
    return args;
}

[[nodiscard]] std::optional<Color> evaluate_solid_color_expression(
    std::string_view expr,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame)
{
    if (expr.empty()) return std::nullopt;

    constexpr std::string_view kSolidPrefix = "solid(";
    if (expr.size() <= kSolidPrefix.size() ||
        expr.substr(0, kSolidPrefix.size()) != kSolidPrefix)
        return std::nullopt;

    int depth = 1;
    size_t close = kSolidPrefix.size();
    for (; close < expr.size() && depth > 0; ++close) {
        char c = expr[close];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
    }

    if (depth != 0 || close <= kSolidPrefix.size())
        return std::nullopt;

    const size_t inner_start = kSolidPrefix.size();
    const size_t inner_len = close - 1 - inner_start;
    const std::string_view inner = expr.substr(inner_start, inner_len);

    std::array<std::string_view, 4> args{};
    if (!split_solid_4_args(inner, args))
        return std::nullopt;

    // The variable table is materialised only if some argument is not a plain
    // literal; numeric literals bypass the expression parser entirely.
    const std::unordered_map<std::string, double>* vars = nullptr;
    auto eval_arg = [&](std::string_view arg) -> f32 {
        if (arg.empty()) return 0.0f;
        double literal = 0.0;
        if (parse_literal_number(arg, literal))
            return static_cast<f32>(literal);
        if (vars == nullptr) {
            vars = &solid_color_vars(frame, t, static_cast<double>(fps),
                                     static_cast<double>(ctx.index));
        }
        return static_cast<f32>(math::evaluate_expression(arg, *vars, 0.0));
    };

    const f32 r = std::clamp(eval_arg(args[0]), 0.0f, 1.0f);
    const f32 g = std::clamp(eval_arg(args[1]), 0.0f, 1.0f);
    const f32 b = std::clamp(eval_arg(args[2]), 0.0f, 1.0f);
    const f32 a = std::clamp(eval_arg(args[3]), 0.0f, 1.0f);

    return Color{r, g, b, a};
}

} // namespace detail

graphics::FillStyle evaluate_fill_expression(
    const std::string& expr,
    const graphics::FillStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame)
{
    auto color = detail::evaluate_solid_color_expression(expr, ctx, fps, t, frame);
    return color ? graphics::FillStyle::solid(*color) : base;
}

graphics::StrokeStyle evaluate_stroke_expression(
    const std::string& expr,
    const graphics::StrokeStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame)
{
    auto color = detail::evaluate_solid_color_expression(expr, ctx, fps, t, frame);
    if (!color) return base;

    auto result = base;
    result.color = *color;
    return result;
}

} // namespace chronon3d
