# ==============================================================================
# include/chronon3d/animation/core/detail/animated_value_expressions.hpp
#
# EXPRESSIONS side of AnimatedValue<T> — extracted verbatim from
# include/chronon3d/animation/core/animated_value.hpp as part of the
# Phase-3 mechanical split.  Single source of truth for:
#   • the four expression-related free function DECLARATIONS that
#     AnimatedValue<FillStyle/StrokeStyle/f32> requires at evaluate
#     time (split_expr_args, evaluate_solid_color_color_expression,
#     evaluate_fill_expression, evaluate_stroke_expression). Their
#     implementations live in `src/animations/` (linked into
#     chronon3d_animations OBJECT library) — this header exposes only
#     the surface.
#   • the template method `evaluate(SampleTime, AnimationEvalContext)`
#     that dispatches the expression side per-type (if constexpr T
#     branches for f32, FillStyle, StrokeStyle, fallback to base value).
#
# PRECONDITIONS
#   This header is included at the BOTTOM of animated_value.hpp, AFTER
#   AnimatedValue<T>'s class body is fully defined.  No circular
#   dependency: the detail header references the template class type
#   from the public header.
#
# CONTRACT
#   The free function declarations (split_expr_args, the four
#   evaluators) are unchanged from their pre-split signatures — the
#   `src/animations/` definitions link the same symbols.  The
#   `evaluate(SampleTime, AnimationEvalContext)` template method body
#   is byte-equivalent to the original in the public header.
# ==============================================================================
#pragma once

#include <chronon3d/animation/core/animated_value.hpp>  // for AnimationEvalContext, graphics::FillStyle/StrokeStyle, math::ExpressionContext.

namespace chronon3d {

// ── Free function declarations ───────────────────────────────────────────────
// These are declared in animated_value.hpp's pre-split form and linked
// from src/animations/ — their definitions are NOT moved here.  Keeping
// them as declarations preserves the link surface; downstream callers
// continue to call them unchanged.
//
//   split_expr_args               — split comma-separated args respecting paren nesting
//   evaluate_solid_color_expression — parse "solid(r, g, b, a)" into a Color (optional)
//   evaluate_fill_expression      — evaluate a "solid(...)" expression against an AnimatedValue<FillStyle> base
//   evaluate_stroke_expression    — same but for AnimatedValue<StrokeStyle>
namespace detail {

[[nodiscard]] std::vector<std::string> split_expr_args(
    const std::string& inner);

[[nodiscard]] std::optional<Color> evaluate_solid_color_expression(
    std::string_view expr,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

} // namespace detail

[[nodiscard]] graphics::FillStyle evaluate_fill_expression(
    const std::string& expr,
    const graphics::FillStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

[[nodiscard]] graphics::StrokeStyle evaluate_stroke_expression(
    const std::string& expr,
    const graphics::StrokeStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

} // namespace chronon3d
