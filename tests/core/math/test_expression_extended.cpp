#include <doctest/doctest.h>
#include <chronon3d/math/expression.hpp>
#include <cmath>
#include <limits>
using namespace chronon3d;

using namespace chronon3d::math;

// ═══════════════════════════════════════════════════════════════════════════
// Comparison operators
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: less than") {
    CHECK(evaluate_expression("3 < 5", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("5 < 3", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("3 < 3", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: greater than") {
    CHECK(evaluate_expression("5 > 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("3 > 5", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: less than or equal") {
    CHECK(evaluate_expression("3 <= 5", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("3 <= 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("5 <= 3", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: greater than or equal") {
    CHECK(evaluate_expression("5 >= 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("3 >= 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("3 >= 5", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: equality") {
    CHECK(evaluate_expression("5 == 5", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("5 == 3", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: inequality") {
    CHECK(evaluate_expression("5 != 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("5 != 5", {}, 0.0) == doctest::Approx(0.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// Logical operators
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: logical AND") {
    CHECK(evaluate_expression("1 && 1", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("1 && 0", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("0 && 1", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("0 && 0", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: logical OR") {
    CHECK(evaluate_expression("1 || 0", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("0 || 1", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("1 || 1", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("0 || 0", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: logical NOT") {
    CHECK(evaluate_expression("!0", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("!1", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("!5", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: logical NOT precedence") {
    CHECK(evaluate_expression("!0 && 1", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("!1 || 1", {}, 0.0) == doctest::Approx(1.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// Ternary operator
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: ternary operator") {
    CHECK(evaluate_expression("1 ? 10 : 20", {}, 0.0) == doctest::Approx(10.0));
    CHECK(evaluate_expression("0 ? 10 : 20", {}, 0.0) == doctest::Approx(20.0));
}

TEST_CASE("expression: ternary with condition") {
    std::unordered_map<std::string, double> vars{{"x", 5.0}};
    CHECK(evaluate_expression("x > 3 ? 100 : 200", vars, 0.0) == doctest::Approx(100.0));
    vars["x"] = 1.0;
    CHECK(evaluate_expression("x > 3 ? 100 : 200", vars, 0.0) == doctest::Approx(200.0));
}

TEST_CASE("expression: nested ternary") {
    // a ? b : c ? d : e  ==  a ? b : (c ? d : e)  (right-associative)
    CHECK(evaluate_expression("0 ? 1 : 1 ? 2 : 3", {}, 0.0) == doctest::Approx(2.0));
    CHECK(evaluate_expression("0 ? 1 : 0 ? 2 : 3", {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("1 ? 1 : 1 ? 2 : 3", {}, 0.0) == doctest::Approx(1.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// Modulus operator
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: modulus") {
    CHECK(evaluate_expression("10 % 3", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("9 % 3", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("7 % 4", {}, 0.0) == doctest::Approx(3.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// Exponentiation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: exponentiation") {
    CHECK(evaluate_expression("2 ^ 3", {}, 0.0) == doctest::Approx(8.0));
    CHECK(evaluate_expression("3 ^ 2", {}, 0.0) == doctest::Approx(9.0));
    CHECK(evaluate_expression("2 ^ 0", {}, 0.0) == doctest::Approx(1.0));
}

TEST_CASE("expression: exponentiation is right-associative") {
    // 2^3^2 = 2^(3^2) = 2^9 = 512
    CHECK(evaluate_expression("2 ^ 3 ^ 2", {}, 0.0) == doctest::Approx(512.0));
}

TEST_CASE("expression: exponentiation precedence") {
    // ^ binds tighter than * and -
    CHECK(evaluate_expression("2 * 3 ^ 2", {}, 0.0) == doctest::Approx(18.0));
    CHECK(evaluate_expression("2 ^ 3 * 2", {}, 0.0) == doctest::Approx(16.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-standard functions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: linear remapping (3 args)") {
    // linear(0.5, 0, 100) => 50
    CHECK(evaluate_expression("linear(0.5, 0, 100)", {}, 0.0) == doctest::Approx(50.0));
    // linear(0, 0, 100) => 0
    CHECK(evaluate_expression("linear(0, 0, 100)", {}, 0.0) == doctest::Approx(0.0));
    // linear(1, 0, 100) => 100
    CHECK(evaluate_expression("linear(1, 0, 100)", {}, 0.0) == doctest::Approx(100.0));
    // Clamping: linear(1.5, 0, 100) => 100
    CHECK(evaluate_expression("linear(1.5, 0, 100)", {}, 0.0) == doctest::Approx(100.0));
    CHECK(evaluate_expression("linear(-0.5, 0, 100)", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: linear remapping (5 args)") {
    // linear(t, t_min, t_max, v_min, v_max)
    // linear(50, 0, 100, 0, 1) => 0.5
    CHECK(evaluate_expression("linear(50, 0, 100, 0, 1)", {}, 0.0) == doctest::Approx(0.5));
    // linear(0, 0, 100, 200, 300) => 200
    CHECK(evaluate_expression("linear(0, 0, 100, 200, 300)", {}, 0.0) == doctest::Approx(200.0));
    CHECK(evaluate_expression("linear(100, 0, 100, 200, 300)", {}, 0.0) == doctest::Approx(300.0));
}

TEST_CASE("expression: ease remapping") {
    // ease uses smoothstep: ease(0.5, 0, 100) => 50 (smoothstep(0.5) = 0.5)
    CHECK(evaluate_expression("ease(0.5, 0, 100)", {}, 0.0) == doctest::Approx(50.0));
    // At endpoints
    CHECK(evaluate_expression("ease(0, 0, 100)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("ease(1, 0, 100)", {}, 0.0) == doctest::Approx(100.0));
    // Smoothstep(0.25) = 0.25^2 * (3 - 2*0.25) = 0.0625 * 2.5 = 0.15625
    CHECK(evaluate_expression("ease(0.25, 0, 100)", {}, 0.0) == doctest::Approx(15.625));
}

TEST_CASE("expression: ease 5 args") {
    // ease(t, t_min, t_max, v_min, v_max)
    CHECK(evaluate_expression("ease(50, 0, 100, 0, 200)", {}, 0.0) == doctest::Approx(100.0));
}

TEST_CASE("expression: easeIn") {
    // easeIn uses quadratic: easeIn(frac) = frac^2
    // At 0.5: frac = 0.5, result = 0.25 * 100 = 25
    CHECK(evaluate_expression("easeIn(0.5, 0, 100)", {}, 0.0) == doctest::Approx(25.0));
    CHECK(evaluate_expression("easeIn(0, 0, 100)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("easeIn(1, 0, 100)", {}, 0.0) == doctest::Approx(100.0));
}

TEST_CASE("expression: easeOut") {
    // easeOut uses: 1 - (1-frac)^2
    // At 0.5: 1 - 0.5^2 = 0.75 → 75
    CHECK(evaluate_expression("easeOut(0.5, 0, 100)", {}, 0.0) == doctest::Approx(75.0));
    CHECK(evaluate_expression("easeOut(0, 0, 100)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("easeOut(1, 0, 100)", {}, 0.0) == doctest::Approx(100.0));
}

TEST_CASE("expression: degreesToRadians / radiansToDegrees") {
    CHECK(evaluate_expression("degreesToRadians(180)", {}, 0.0) == doctest::Approx(3.14159265358979).epsilon(0.001));
    CHECK(evaluate_expression("radiansToDegrees(3.14159265358979)", {}, 0.0) == doctest::Approx(180.0).epsilon(0.001));
    CHECK(evaluate_expression("degreesToRadians(90)", {}, 0.0) == doctest::Approx(1.57079632679).epsilon(0.001));
    // Aliases
    CHECK(evaluate_expression("radians(180)", {}, 0.0) == doctest::Approx(3.14159265358979).epsilon(0.001));
    CHECK(evaluate_expression("degrees(3.14159265358979)", {}, 0.0) == doctest::Approx(180.0).epsilon(0.001));
}

TEST_CASE("expression: additional math functions") {
    CHECK(evaluate_expression("asin(0)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("acos(1)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("atan(0)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("atan2(1, 1)", {}, 0.0) == doctest::Approx(0.785398163).epsilon(0.001));
    CHECK(evaluate_expression("exp(0)", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("log(1)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("ceil(2.3)", {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("floor(2.7)", {}, 0.0) == doctest::Approx(2.0));
    CHECK(evaluate_expression("round(2.5)", {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("round(2.4)", {}, 0.0) == doctest::Approx(2.0));
    CHECK(evaluate_expression("sign(-5)", {}, 0.0) == doctest::Approx(-1.0));
    CHECK(evaluate_expression("sign(5)", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("sign(0)", {}, 0.0) == doctest::Approx(0.0));
    CHECK(evaluate_expression("trunc(2.7)", {}, 0.0) == doctest::Approx(2.0));
    CHECK(evaluate_expression("trunc(-2.7)", {}, 0.0) == doctest::Approx(-2.0));
    CHECK(evaluate_expression("pow(2, 10)", {}, 0.0) == doctest::Approx(1024.0));
    CHECK(evaluate_expression("log10(1000)", {}, 0.0) == doctest::Approx(3.0));
}

TEST_CASE("expression: constants") {
    CHECK(evaluate_expression("PI", {}, 0.0) == doctest::Approx(3.14159265358979).epsilon(0.001));
    CHECK(evaluate_expression("E", {}, 0.0) == doctest::Approx(2.71828182845904).epsilon(0.001));
    CHECK(evaluate_expression("true", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("false", {}, 0.0) == doctest::Approx(0.0));
}

// ═══════════════════════════════════════════════════════════════════════════
// ExpressionContext + cross-layer references
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: context variables") {
    ExpressionContext ctx;
    ctx = ctx.with_frame(30.0);
    ctx.time = 1.0;
    ctx.fps = 30.0;
    ctx.width = 1920.0;
    ctx.height = 1080.0;
    ctx.index = 5.0;
    ctx.num_layers = 10.0;
    ctx.value = 42.0;

    CHECK(evaluate_expression("frame", ctx, {}, 0.0) == doctest::Approx(30.0));
    CHECK(evaluate_expression("time", ctx, {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("fps", ctx, {}, 0.0) == doctest::Approx(30.0));
    CHECK(evaluate_expression("width", ctx, {}, 0.0) == doctest::Approx(1920.0));
    CHECK(evaluate_expression("height", ctx, {}, 0.0) == doctest::Approx(1080.0));
    CHECK(evaluate_expression("index", ctx, {}, 0.0) == doctest::Approx(5.0));
    CHECK(evaluate_expression("numLayers", ctx, {}, 0.0) == doctest::Approx(10.0));
    CHECK(evaluate_expression("value", ctx, {}, 0.0) == doctest::Approx(42.0));
}

TEST_CASE("expression: thisComp.width / thisComp.height") {
    ExpressionContext ctx;
    ctx.width = 1920.0;
    ctx.height = 1080.0;
    ctx.num_layers = 5.0;

    CHECK(evaluate_expression("thisComp.width", ctx, {}, 0.0) == doctest::Approx(1920.0));
    CHECK(evaluate_expression("thisComp.height", ctx, {}, 0.0) == doctest::Approx(1080.0));
    CHECK(evaluate_expression("thisComp.numLayers", ctx, {}, 0.0) == doctest::Approx(5.0));
}

TEST_CASE("expression: thisLayer properties") {
    ExpressionContext ctx;
    ctx.index = 3.0;
    ctx.in_point = 0.5;
    ctx.out_point = 5.0;
    ctx.width_0 = 1920.0;
    ctx.height_0 = 1080.0;
    ctx.value = 75.0;

    CHECK(evaluate_expression("thisLayer.index", ctx, {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("thisLayer.inPoint", ctx, {}, 0.0) == doctest::Approx(0.5));
    CHECK(evaluate_expression("thisLayer.outPoint", ctx, {}, 0.0) == doctest::Approx(5.0));
    CHECK(evaluate_expression("thisLayer.width", ctx, {}, 0.0) == doctest::Approx(1920.0));
    CHECK(evaluate_expression("thisLayer.height", ctx, {}, 0.0) == doctest::Approx(1080.0));
    CHECK(evaluate_expression("thisLayer.opacity", ctx, {}, 0.0) == doctest::Approx(75.0));
}

TEST_CASE("expression: thisProperty") {
    ExpressionContext ctx;
    ctx.value = 50.0;
    ctx.value0 = 10.0;
    ctx.value1 = 20.0;
    ctx.value2 = 30.0;

    CHECK(evaluate_expression("thisProperty.value", ctx, {}, 0.0) == doctest::Approx(50.0));
    CHECK(evaluate_expression("thisProperty.value0", ctx, {}, 0.0) == doctest::Approx(10.0));
    CHECK(evaluate_expression("thisProperty.value1", ctx, {}, 0.0) == doctest::Approx(20.0));
    CHECK(evaluate_expression("thisProperty.value2", ctx, {}, 0.0) == doctest::Approx(30.0));
}

TEST_CASE("expression: cross-layer reference via layer resolver") {
    ExpressionContext ctx;
    ctx.time = 1.0;
    ctx = ctx.with_frame(30.0);
    ctx.value = 50.0;

    // Mock resolver: "bg" layer opacity = 0.8, position.x = 100
    ctx.layer_resolver = [](const std::string& layer_name,
                            const std::string& prop_path,
                            double /*time*/) -> double {
        if (layer_name == "bg") {
            if (prop_path == "opacity") return 0.8;
            if (prop_path == "transform.opacity") return 0.8;
            if (prop_path == "position.x") return 100.0;
            if (prop_path == "transform.position.x") return 100.0;
        }
        return std::numeric_limits<double>::quiet_NaN();
    };

    CHECK(evaluate_expression("layer('bg').opacity", ctx, {}, 0.0) == doctest::Approx(0.8));
    CHECK(evaluate_expression("layer('bg').position.x", ctx, {}, 0.0) == doctest::Approx(100.0));
    CHECK(evaluate_expression("layer('bg').transform.opacity", ctx, {}, 0.0) == doctest::Approx(0.8));
}

TEST_CASE("expression: cross-layer in arithmetic") {
    ExpressionContext ctx;
    ctx.value = 50.0;
    ctx.layer_resolver = [](const std::string& ln, const std::string& pp, double) -> double {
        if (ln == "ctrl" && pp == "opacity") return 0.5;
        return std::numeric_limits<double>::quiet_NaN();
    };

    // Multiply current value by controller layer opacity
    CHECK(evaluate_expression("value * layer('ctrl').opacity", ctx, {}, 0.0) == doctest::Approx(25.0));
}

TEST_CASE("expression: cross-layer with ternary") {
    ExpressionContext ctx;
    ctx.value = 100.0;
    ctx.layer_resolver = [](const std::string& ln, const std::string& pp, double) -> double {
        if (ln == "flag" && pp == "opacity") return 1.0;
        return std::numeric_limits<double>::quiet_NaN();
    };

    CHECK(evaluate_expression("layer('flag').opacity ? value * 2 : value", ctx, {}, 0.0) == doctest::Approx(200.0));
}

TEST_CASE("expression: missing layer falls back") {
    ExpressionContext ctx;
    ctx.value = 42.0;
    ctx.layer_resolver = [](const std::string&, const std::string&, double) -> double {
        return std::numeric_limits<double>::quiet_NaN(); // layer not found
    };

    // NaN from resolver → expression error → fallback
    double result = evaluate_expression("layer('nonexistent').opacity", ctx, {}, -1.0);
    bool ok = std::isnan(result) || (result == -1.0);
    CHECK(ok);
}

// ═══════════════════════════════════════════════════════════════════════════
// seedRandom / random
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: seedRandom makes random deterministic") {
    ExpressionContext ctx;

    // Use separate calls with shared state to simulate semicoloned statements
    ExpressionState state1, state2;

    // Step 1: seed both states with seed 42
    evaluate_expression("seedRandom(42)", ctx, {}, state1, 0.0);
    evaluate_expression("seedRandom(42)", ctx, {}, state2, 0.0);

    // Step 2: generate random from both — should be identical
    double r1 = evaluate_expression("random()", ctx, {}, state1, 0.0);
    double r2 = evaluate_expression("random()", ctx, {}, state2, 0.0);

    CHECK(r1 == doctest::Approx(r2));
    // Verify it's actually producing non-zero values
    CHECK(r1 >= 0.0);
    CHECK(r1 < 1.0);
}

TEST_CASE("expression: random with min/max range") {
    ExpressionContext ctx;
    ExpressionState state;

    // random(max) → [0, max)
    double r = evaluate_expression("random(100)", ctx, {}, state, -1.0);
    CHECK(r >= 0.0);
    CHECK(r < 100.0);

    // random(min, max) → [min, max)
    state = ExpressionState{};
    evaluate_expression("seedRandom(7)", ctx, {}, state, 0.0);
    double r2 = evaluate_expression("random(50, 150)", ctx, {}, state, -1.0);
    CHECK(r2 >= 50.0);
    CHECK(r2 < 150.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Wiggle
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: wiggle is deterministic") {
    ExpressionContext ctx;
    ctx.time = 1.0;

    double w1 = evaluate_expression("wiggle(3, 10, 1.0, 0)", ctx, {}, 0.0);
    double w2 = evaluate_expression("wiggle(3, 10, 1.0, 0)", ctx, {}, 0.0);
    CHECK(w1 == doctest::Approx(w2));

    // Different time should give different result
    double w3 = evaluate_expression("wiggle(3, 10, 2.0, 0)", ctx, {}, 0.0);
    // (may coincidentally be the same, but very unlikely)
    // Just verify it doesn't error
    CHECK(!std::isnan(w3));
}

// ═══════════════════════════════════════════════════════════════════════════
// Complex expressions combining multiple features
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: comparison + ternary combo") {
    std::unordered_map<std::string, double> vars{{"frame", 60.0}};
    // "frame > 30 ? 100 : 0" → 100
    CHECK(evaluate_expression("frame > 30 ? 100 : 0", vars, 0.0) == doctest::Approx(100.0));
    vars["frame"] = 10.0;
    CHECK(evaluate_expression("frame > 30 ? 100 : 0", vars, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: clamp with variables and math") {
    std::unordered_map<std::string, double> vars{{"time", 2.0}};
    // clamp(sin(time) * 100, 0, 50)
    double expected = std::clamp(std::sin(2.0) * 100.0, 0.0, 50.0);
    CHECK(evaluate_expression("clamp(sin(time) * 100, 0, 50)", vars, 0.0) == doctest::Approx(expected));
}

TEST_CASE("expression: linear with frame variable") {
    std::unordered_map<std::string, double> vars{{"frame", 50.0}};
    // Remap frame [0, 100] to [0, 1]
    CHECK(evaluate_expression("linear(frame, 0, 100, 0, 1)", vars, 0.0) == doctest::Approx(0.5));
}

TEST_CASE("expression: ease with frame in AE-style animation") {
    std::unordered_map<std::string, double> vars{{"frame", 15.0}, {"time", 0.5}};
    // ease(time, 0, 1, 0, 100) — smoothstep over 1 second
    // smoothstep(0.5) = 0.5
    CHECK(evaluate_expression("ease(time, 0, 1, 0, 100)", vars, 0.0) == doctest::Approx(50.0));
}

TEST_CASE("expression: backward compatibility — legacy API unchanged") {
    // The old API should work exactly as before
    CHECK(evaluate_expression("2 + 3", {}, 0.0) == doctest::Approx(5.0));
    CHECK(evaluate_expression("sin(0)", {}, 0.0) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(evaluate_expression("clamp(5, 0, 3)", {}, 0.0) == doctest::Approx(3.0));
    CHECK(evaluate_expression("undefined_var", {}, -999.0) == doctest::Approx(-999.0));
}

TEST_CASE("expression: context-aware API with empty layer resolver") {
    ExpressionContext ctx;
    ctx = ctx.with_frame(10.0);
    ctx.time = 0.5;

    // Should still work for basic expressions even with empty resolver
    CHECK(evaluate_expression("frame + time", ctx, {}, 0.0) == doctest::Approx(10.5));
    CHECK(evaluate_expression("sin(time * 2)", ctx, {}, 0.0) == doctest::Approx(std::sin(1.0)).epsilon(1e-6));
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases and error handling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("expression: division by zero returns infinity") {
    CHECK(evaluate_expression("1 / 0", {}, 0.0) == std::numeric_limits<double>::infinity());
}

TEST_CASE("expression: modulus by zero returns zero") {
    CHECK(evaluate_expression("5 % 0", {}, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("expression: negative exponentiation") {
    CHECK(evaluate_expression("2 ^ -1", {}, 0.0) == doctest::Approx(0.5));
}

TEST_CASE("expression: complex operator precedence chain") {
    // 2 + 3 * 4 > 10 && 1 || 0
    // = 2 + 12 > 10 && 1 || 0
    // = 14 > 10 && 1 || 0
    // = 1 && 1 || 0
    // = 1 || 0
    // = 1
    CHECK(evaluate_expression("2 + 3 * 4 > 10 && 1 || 0", {}, 0.0) == doctest::Approx(1.0));
}

TEST_CASE("expression: double negation") {
    CHECK(evaluate_expression("--5", {}, 0.0) == doctest::Approx(5.0));
}

TEST_CASE("expression: NOT with comparison") {
    CHECK(evaluate_expression("!(3 > 5)", {}, 0.0) == doctest::Approx(1.0));
    CHECK(evaluate_expression("!(5 > 3)", {}, 0.0) == doctest::Approx(0.0));
}
