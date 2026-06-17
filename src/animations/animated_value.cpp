#include <chronon3d/animation/core/animated_value.hpp>

#include <chronon3d/math/expression.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace chronon3d {

namespace detail {

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

} // namespace detail

graphics::FillStyle evaluate_fill_expression(
    const std::string& expr,
    const graphics::FillStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame)
{
    if (expr.empty()) return base;

    constexpr std::string_view kSolidPrefix = "solid(";
    if (expr.size() > kSolidPrefix.size() &&
        expr.substr(0, kSolidPrefix.size()) == kSolidPrefix)
    {
        int depth = 1;
        size_t close = kSolidPrefix.size();
        for (; close < expr.size() && depth > 0; ++close) {
            char c = expr[close];
            if (c == '(') ++depth;
            else if (c == ')') --depth;
        }

        if (depth == 0 && close > kSolidPrefix.size()) {
            const size_t inner_start = kSolidPrefix.size();
            const size_t inner_len = close - 1 - inner_start;
            const std::string inner = expr.substr(inner_start, inner_len);
            const auto args = detail::split_expr_args(inner);

            if (args.size() == 4) {
                const std::unordered_map<std::string, double> vars{
                    {"frame", frame},
                    {"time", t},
                    {"fps", static_cast<double>(fps)},
                    {"index", static_cast<double>(ctx.index)},
                };

                auto eval_arg = [&](const std::string& arg) -> f32 {
                    if (arg.empty()) return 0.0f;
                    return static_cast<f32>(
                        math::evaluate_expression(arg, vars, 0.0)
                    );
                };

                const f32 r = std::clamp(eval_arg(args[0]), 0.0f, 1.0f);
                const f32 g = std::clamp(eval_arg(args[1]), 0.0f, 1.0f);
                const f32 b = std::clamp(eval_arg(args[2]), 0.0f, 1.0f);
                const f32 a = std::clamp(eval_arg(args[3]), 0.0f, 1.0f);

                return graphics::FillStyle::solid(Color{r, g, b, a});
            }
        }
    }

    return base;
}

graphics::StrokeStyle evaluate_stroke_expression(
    const std::string& expr,
    const graphics::StrokeStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame)
{
    if (expr.empty()) return base;

    constexpr std::string_view kSolidPrefix = "solid(";
    if (expr.size() > kSolidPrefix.size() &&
        expr.substr(0, kSolidPrefix.size()) == kSolidPrefix)
    {
        int depth = 1;
        size_t close = kSolidPrefix.size();
        for (; close < expr.size() && depth > 0; ++close) {
            char c = expr[close];
            if (c == '(') ++depth;
            else if (c == ')') --depth;
        }

        if (depth == 0 && close > kSolidPrefix.size()) {
            const size_t inner_start = kSolidPrefix.size();
            const size_t inner_len = close - 1 - inner_start;
            const std::string inner = expr.substr(inner_start, inner_len);
            const auto args = detail::split_expr_args(inner);

            if (args.size() == 4) {
                const std::unordered_map<std::string, double> vars{
                    {"frame", frame},
                    {"time", t},
                    {"fps", static_cast<double>(fps)},
                    {"index", static_cast<double>(ctx.index)},
                };

                auto eval_arg = [&](const std::string& arg) -> f32 {
                    if (arg.empty()) return 0.0f;
                    return static_cast<f32>(
                        math::evaluate_expression(arg, vars, 0.0)
                    );
                };

                const f32 r = std::clamp(eval_arg(args[0]), 0.0f, 1.0f);
                const f32 g = std::clamp(eval_arg(args[1]), 0.0f, 1.0f);
                const f32 b = std::clamp(eval_arg(args[2]), 0.0f, 1.0f);
                const f32 a = std::clamp(eval_arg(args[3]), 0.0f, 1.0f);

                graphics::StrokeStyle result = base;
                result.color = Color{r, g, b, a};
                return result;
            }
        }
    }

    return base;
}

} // namespace chronon3d
