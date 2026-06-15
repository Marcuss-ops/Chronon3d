#include <doctest/doctest.h>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::graphics;

// ── Helper: approximate float equality ─────────────────────────────────

static bool approx(f32 a, f32 b, f32 eps = 1e-5f) {
    return std::abs(a - b) < eps;
}

static bool color_approx(const Color& a, const Color& b, f32 eps = 1e-5f) {
    return approx(a.r, b.r, eps) && approx(a.g, b.g, eps) &&
           approx(a.b, b.b, eps) && approx(a.a, b.a, eps);
}

TEST_CASE("GradientDefinition — linear two-stop") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        });

    // At t=0: black (linear-space black is 0,0,0)
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::black().to_linear()));

    // At t=1: white
    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::white().to_linear()));

    // At t=0.5: mid-grey in linear space (0.5 between black [0,0,0] and white [1,1,1])
    Color cm = sample_gradient(def, 0.5f);
    CHECK(approx(cm.r, 0.5f));
    CHECK(approx(cm.r, cm.g));
    CHECK(approx(cm.g, cm.b));
}

TEST_CASE("GradientDefinition — three-stop linear") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::red()},
            {0.5f, Color::green()},
            {1.0f, Color::blue()},
        });

    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::red().to_linear()));

    Color c05 = sample_gradient(def, 0.5f);
    CHECK(color_approx(c05, Color::green().to_linear()));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::blue().to_linear()));

    // Between stops: interpolated
    Color c025 = sample_gradient(def, 0.25f);
    CHECK(c025.r > 0.4f);  // red→green, red component drops
    CHECK(c025.g > 0.4f);  // green component rises
}

TEST_CASE("GradientDefinition — radial") {
    auto def = GradientDefinition::radial(
        {0.5f, 0.5f}, 0.5f,
        {
            {0.0f, Color::white()},
            {1.0f, Color::black()},
        });

    // Center: white
    Color cc = sample_gradient_radial(def, {0.5f, 0.5f});
    CHECK(color_approx(cc, Color::white().to_linear()));

    // Edge: black
    Color ce = sample_gradient_radial(def, {1.0f, 0.5f});
    CHECK(color_approx(ce, Color::black().to_linear()));

    // Midpoint: grey
    Color cm = sample_gradient_radial(def, {0.75f, 0.5f});
    CHECK(cm.r < 1.0f);
    CHECK(cm.r > 0.0f);
}

TEST_CASE("GradientDefinition — spread Pad (default)") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        },
        GradientSpread::Pad);

    // Below 0 → clamped to black
    Color below = sample_gradient(def, -0.5f);
    CHECK(color_approx(below, Color::black().to_linear()));

    // Above 1 → clamped to white
    Color above = sample_gradient(def, 1.5f);
    CHECK(color_approx(above, Color::white().to_linear()));
}

TEST_CASE("GradientDefinition — spread Repeat") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        },
        GradientSpread::Repeat);

    // t=0 and t=1 should give the same result
    Color c0 = sample_gradient(def, 0.0f);
    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c0, c1));

    // t=0.25 and t=1.25 should give the same result
    Color c025  = sample_gradient(def, 0.25f);
    Color c125  = sample_gradient(def, 1.25f);
    CHECK(color_approx(c025, c125));

    // Negative t wraps
    Color c_neg = sample_gradient(def, -0.5f);
    Color c05   = sample_gradient(def, 0.5f);
    CHECK(color_approx(c_neg, c05));
}

TEST_CASE("GradientDefinition — spread Reflect") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        },
        GradientSpread::Reflect);

    // t=0.5 and t=1.5 should give the same (mirror at 1 folds back)
    Color c05  = sample_gradient(def, 0.5f);
    Color c15  = sample_gradient(def, 1.5f);
    CHECK(color_approx(c05, c15));
}

TEST_CASE("GradientDefinition — opacity stops") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::white()},
            {1.0f, Color::white()},
        });
    def.opacity_stops = {
        {0.0f, 1.0f},
        {0.5f, 0.0f},
        {1.0f, 1.0f},
    };

    Color c0 = sample_gradient(def, 0.0f);
    CHECK(approx(c0.a, 1.0f));

    Color c05 = sample_gradient(def, 0.5f);
    CHECK(approx(c05.a, 0.0f));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(approx(c1.a, 1.0f));
}

TEST_CASE("GradientDefinition — empty stops fallback") {
    GradientDefinition def;
    def.type = GradientType::Linear;

    Color c = sample_gradient(def, 0.5f);
    // Should return opaque white (fallback)
    CHECK(approx(c.r, 1.0f));
    CHECK(approx(c.g, 1.0f));
    CHECK(approx(c.b, 1.0f));
    CHECK(approx(c.a, 1.0f));
}

TEST_CASE("GradientDefinition — single stop") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.5f, Color::red()},
        });

    // Single stop → always that colour
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::red().to_linear()));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::red().to_linear()));
}

// ───────────────────────────────────────────────────────────────────────
//  DoD: Geometry edge cases
// ───────────────────────────────────────────────────────────────────────

TEST_CASE("GradientDefinition — point before start (t < 0)") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::red()},
            {1.0f, Color::blue()},
        },
        GradientSpread::Pad);

    // t = -0.5 → clamped to first stop (red)
    Color c = sample_gradient(def, -0.5f);
    CHECK(color_approx(c, Color::red().to_linear()));
}

TEST_CASE("GradientDefinition — point after end (t > 1)") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::red()},
            {1.0f, Color::blue()},
        },
        GradientSpread::Pad);

    // t = 2.0 → clamped to last stop (blue)
    Color c = sample_gradient(def, 2.0f);
    CHECK(color_approx(c, Color::blue().to_linear()));
}

TEST_CASE("GradientDefinition — vertical gradient direction") {
    auto def = GradientDefinition::linear(
        {0.5f, 0.0f}, {0.5f, 1.0f},
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        });

    // t=0 → black, t=1 → white
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::black().to_linear()));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::white().to_linear()));

    // Geometry is stored correctly (caller computes t from point)
    CHECK(approx(def.start.x, 0.5f));
    CHECK(approx(def.start.y, 0.0f));
    CHECK(approx(def.end.x, 0.5f));
    CHECK(approx(def.end.y, 1.0f));
}

TEST_CASE("GradientDefinition — diagonal gradient direction") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 1.0f},
        {
            {0.0f, Color::black()},
            {0.5f, Color::red()},
            {1.0f, Color::white()},
        });

    // At t=0.5 the colour should be exactly red
    Color cm = sample_gradient(def, 0.5f);
    CHECK(color_approx(cm, Color::red().to_linear()));
}

TEST_CASE("GradientDefinition — start equals end (degenerate)") {
    auto def = GradientDefinition::linear(
        {0.5f, 0.5f}, {0.5f, 0.5f},  // zero-length direction
        {
            {0.0f, Color::red()},
            {1.0f, Color::blue()},
        });

    // Degenerate geometry: sampling function still works (t is caller-provided).
    // The geometry is stored as-is; caller should detect zero-length direction.
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::red().to_linear()));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::blue().to_linear()));

    // Must not produce NaN
    CHECK(!std::isnan(c0.r));
    CHECK(!std::isnan(c1.r));
}

// ───────────────────────────────────────────────────────────────────────
//  DoD: Stop spacing edge cases
// ───────────────────────────────────────────────────────────────────────

TEST_CASE("GradientDefinition — non-equidistant stops") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::red()},
            {0.2f, Color::green()},
            {1.0f, Color::blue()},
        });

    // At t=0.1: halfway between red (0.0) and green (0.2)
    Color c01 = sample_gradient(def, 0.1f);
    Color r_lin = Color::red().to_linear();
    Color g_lin = Color::green().to_linear();
    CHECK(approx(c01.r, (r_lin.r + g_lin.r) * 0.5f));
    CHECK(approx(c01.g, (r_lin.g + g_lin.g) * 0.5f));

    // At t=0.6: halfway between green (0.2) and blue (1.0)
    // local_t = (0.6 - 0.2) / (1.0 - 0.2) = 0.4 / 0.8 = 0.5
    Color c06 = sample_gradient(def, 0.6f);
    Color b_lin = Color::blue().to_linear();
    CHECK(approx(c06.r, g_lin.r + (b_lin.r - g_lin.r) * 0.5f));
    CHECK(approx(c06.g, g_lin.g + (b_lin.g - g_lin.g) * 0.5f));
}

TEST_CASE("GradientDefinition — stops with same position (hard colour stop)") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::red()},
            {0.5f, Color::green()},
            {0.5f, Color::blue()},   // same position → stable_sort preserves order: green first
            {1.0f, Color::white()},
        });

    // At t=0.49: interpolating red→green (green is the first stop at 0.5)
    Color c_before = sample_gradient(def, 0.49f);
    CHECK(c_before.r > 0.0f);
    CHECK(c_before.b < 0.5f);

    // At t=0.5: lower_bound finds first 0.5 stop (green, thanks to stable_sort).
    // local_t = (0.5 - 0.0) / (0.5 - 0.0) = 1.0 → green.
    Color c_at = sample_gradient(def, 0.5f);
    CHECK(color_approx(c_at, Color::green().to_linear()));

    // At t=0.51: lower_bound finds 1.0 (white). Previous is second 0.5 stop (blue).
    // local_t = (0.51 - 0.5) / (1.0 - 0.5) = 0.02 → mostly blue.
    Color c_after = sample_gradient(def, 0.51f);
    CHECK(c_after.b > c_after.g);
}

TEST_CASE("GradientDefinition — stops provided out of order are sorted") {
    // Feed stops in reverse order; factory must sort them.
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {1.0f, Color::white()},
            {0.0f, Color::black()},
            {0.5f, Color::red()},
        });

    // After sorting: [0.0 black, 0.5 red, 1.0 white]
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::black().to_linear()));

    Color c05 = sample_gradient(def, 0.5f);
    CHECK(color_approx(c05, Color::red().to_linear()));

    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::white().to_linear()));
}

TEST_CASE("GradientDefinition — stops missing at 0 and 1") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.3f, Color::red()},
            {0.7f, Color::blue()},
        });

    // t < 0.3 → clamped to first stop (red)
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(color_approx(c0, Color::red().to_linear()));

    // t > 0.7 → clamped to last stop (blue)
    Color c1 = sample_gradient(def, 1.0f);
    CHECK(color_approx(c1, Color::blue().to_linear()));

    // t between stops → interpolated
    Color c05 = sample_gradient(def, 0.5f);
    Color r_lin = Color::red().to_linear();
    Color b_lin = Color::blue().to_linear();
    f32 local_t = (0.5f - 0.3f) / (0.7f - 0.3f);  // = 0.5
    CHECK(approx(c05.r, r_lin.r + (b_lin.r - r_lin.r) * local_t));
}

// ───────────────────────────────────────────────────────────────────────
//  DoD: Alpha handling
// ───────────────────────────────────────────────────────────────────────

TEST_CASE("GradientDefinition — different alpha between stops") {
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color{1.0f, 0.0f, 0.0f, 0.2f}},   // red, 20% alpha
            {1.0f, Color{0.0f, 0.0f, 1.0f, 1.0f}},    // blue, 100% alpha
        });

    // t=0: red with alpha ~0.2
    Color c0 = sample_gradient(def, 0.0f);
    CHECK(approx(c0.a, 0.2f));

    // t=1: blue with alpha ~1.0
    Color c1 = sample_gradient(def, 1.0f);
    CHECK(approx(c1.a, 1.0f));

    // t=0.5: mid-alpha (~0.6)
    Color cm = sample_gradient(def, 0.5f);
    CHECK(approx(cm.a, 0.6f));
}

// ───────────────────────────────────────────────────────────────────────
//  DoD: Radial edge cases
// ───────────────────────────────────────────────────────────────────────

TEST_CASE("GradientDefinition — DoD interpolation contract (red@0.2, blue@0.6)") {
    // Exact DoD example: red at 0.20, blue at 0.60.
    // At t=0.40: local_t = (0.40-0.20)/(0.60-0.20) = 0.5 → exact midpoint.
    auto def = GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.2f, Color::red()},
            {0.6f, Color::blue()},
        });

    Color r_lin = Color::red().to_linear();
    Color b_lin = Color::blue().to_linear();

    // t=0.20: red
    Color c020 = sample_gradient(def, 0.2f);
    CHECK(color_approx(c020, r_lin));

    // t=0.60: blue
    Color c060 = sample_gradient(def, 0.6f);
    CHECK(color_approx(c060, b_lin));

    // t=0.40: exact midpoint → (r+g)/2 for each channel
    Color c040 = sample_gradient(def, 0.4f);
    CHECK(approx(c040.r, (r_lin.r + b_lin.r) * 0.5f));
    CHECK(approx(c040.g, (r_lin.g + b_lin.g) * 0.5f));
    CHECK(approx(c040.b, (r_lin.b + b_lin.b) * 0.5f));
}

TEST_CASE("GradientDefinition — radial with radius zero") {
    auto def = GradientDefinition::radial(
        {0.5f, 0.5f}, 0.0f,  // zero radius
        {
            {0.0f, Color::white()},
            {1.0f, Color::black()},
        });

    // Radius zero → t = 0 always (guarded by 1e-6f check in sample_gradient_radial)
    Color cc = sample_gradient_radial(def, {0.5f, 0.5f});
    CHECK(color_approx(cc, Color::white().to_linear()));

    // Even far away → still t=0
    Color cf = sample_gradient_radial(def, {100.0f, 100.0f});
    CHECK(color_approx(cf, Color::white().to_linear()));

    // No NaN
    CHECK(!std::isnan(cc.r));
    CHECK(!std::isnan(cf.r));
}

TEST_CASE("GradientDefinition — radial spread Repeat") {
    auto def = GradientDefinition::radial(
        {0.5f, 0.5f}, 0.25f,
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        },
        GradientSpread::Repeat);

    // At radius*1.5 (t=1.5 → wraps to 0.5): should be mid-grey
    Color c15 = sample_gradient_radial(def, {0.5f + 0.375f, 0.5f});
    Color c05 = sample_gradient_radial(def, {0.5f + 0.125f, 0.5f});
    CHECK(color_approx(c15, c05));
}

TEST_CASE("GradientDefinition — radial spread Reflect") {
    auto def = GradientDefinition::radial(
        {0.5f, 0.5f}, 0.25f,
        {
            {0.0f, Color::black()},
            {1.0f, Color::white()},
        },
        GradientSpread::Reflect);

    // At radius*1.5 (t=1.5 → reflects to 0.5): should match t=0.5
    Color c15 = sample_gradient_radial(def, {0.5f + 0.375f, 0.5f});
    Color c05 = sample_gradient_radial(def, {0.5f + 0.125f, 0.5f});
    CHECK(color_approx(c15, c05));
}
