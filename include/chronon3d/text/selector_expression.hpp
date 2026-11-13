#pragma once

// ── Closed selector-expression profile ────────────────────────────────────
//
// AE selector expressions operate on per-glyph inputs, not on the host scene
// graph.  This header is the single place that defines the accepted profile:
// `textIndex`, `textTotal`, `selectorValue`, `time`, `frame`, `fps`, plus the
// host-free built-ins already implemented by math::ExpressionParser.
//
// Host-dependent forms (`thisComp`, `thisLayer`, `thisProperty`, `layer()`,
// `loopIn`/`loopOut`, `valueAtTime`, `toComp`/`fromComp`, `posterizeTime`) are
// rejected by ExpressionProfile::Selector.  Decode and compile both call this
// validator so an unsupported expression cannot silently evaluate to 0.

#include <chronon3d/math/expression.hpp>

#include <string_view>
#include <unordered_map>

namespace chronon3d {

[[nodiscard]] inline bool validate_selector_expression(
    std::string_view expression) {
    std::unordered_map<std::string, double> variables;
    variables.emplace("textIndex", 0.0);
    variables.emplace("textTotal", 1.0);
    variables.emplace("selectorValue", 0.0);

    math::ExpressionContext context;
    context.frame = 0.0;
    context.time = 0.0;
    context.fps = 30.0;

    return math::validate_expression(
        expression, context, variables, math::ExpressionProfile::Selector);
}

} // namespace chronon3d
