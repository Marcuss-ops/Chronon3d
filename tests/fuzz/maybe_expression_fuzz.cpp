// ---------------------------------------------------------------------------
// tests/fuzz/maybe_expression_fuzz.cpp — libFuzzer target for the AE-style
// expression parser (include/chronon3d/math/expression.hpp)
//
// The expression parser is a recursive-descent parser reachable from the
// animation subsystem (`AnimatedValue::expression`, `evaluate_fill_expression`,
// `evaluate_stroke_expression`, `evaluate_solid_color_expression`).  It is a
// pure, header-only parser — no engine, no backend — so it fuzzes directly.
//
// Surface exercised:
//   • arbitrary bytes interpreted as a std::string_view (NULs included)
//   • legacy `evaluate_expression(expr, vars, fallback)` path
//   • context-aware `evaluate_expression(expr, ctx, vars, fallback)` path
//   • state-carrying path (seedRandom/random/wiggle state persistence)
//   • postfix resolution: thisComp.*, thisLayer.*, layer("x").prop[i]
//
// The parser must never crash, overflow, or recurse without bound on any
// input (including pathological `!!!!...`, `((((...`, `1^1^1^...`).
// ---------------------------------------------------------------------------

#include <chronon3d/math/expression.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

using namespace chronon3d::math;

namespace {

// Deterministic cross-layer resolver so the postfix / layer("…").prop paths
// get exercised (a default-constructed std::function short-circuits them).
double layer_resolver(const std::string& name, const std::string& prop, double) {
    double acc = 0.0;
    for (char c : name) acc += static_cast<unsigned char>(c);
    for (char c : prop) acc += static_cast<unsigned char>(c);
    return acc;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // The parser reads a std::string_view with explicit length, so arbitrary
    // bytes (including embedded NULs) are valid inputs.
    const std::string_view expr(reinterpret_cast<const char*>(data), size);

    // 1. Legacy path (no context).
    const std::unordered_map<std::string, double> legacy_vars{
        {"x", 1.5}, {"y", 2.5}, {"pi", 3.14159}};
    (void)evaluate_expression(expr, legacy_vars, /*fallback=*/0.0);

    // 2. Context-aware path with a real layer resolver.
    ExpressionContext ctx;
    ctx.time     = 2.0;
    ctx.frame    = 60.0;
    ctx.fps      = 30.0;
    ctx.width    = 1920.0;
    ctx.height   = 1080.0;
    ctx.value    = 42.0;
    ctx.layer_resolver = layer_resolver;

    const std::unordered_map<std::string, double> extra_vars{};
    (void)evaluate_expression(expr, ctx, extra_vars, /*fallback=*/0.0);

    // 3. State-carrying path (exercises seedRandom / random / wiggle state).
    ExpressionState state;
    state.random_seed = static_cast<unsigned int>(size);
    (void)evaluate_expression(expr, ctx, extra_vars, state, /*fallback=*/0.0);

    return 0;
}