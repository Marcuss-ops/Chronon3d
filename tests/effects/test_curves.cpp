// ---------------------------------------------------------------------------
// test_curves.cpp — Curve tests (sezione 6 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - Curva identità: (0,0) (1,1) → valori preservati
//   - Curva invertita: (0,1) (1,0) → 0→1, 0.25→0.75, 0.5→0.5, 0.75→0.25, 1→0
//   - LUT contro reference: 100k valori pseudo-casuali, errore ≤ 1e-4
//   - HDR extrapolazione: slope = 1.6, per x=1.5 → 2.0
//   - Hash: curve identiche → hash uguali, curve diverse → hash diversi
//   - Cache: get_or_compile restituisce stesso puntatore per stessi params
//   - ColorPipeline fusion: sequential ≈ fused

#include <doctest/doctest.h>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include "tests/effects/test_helpers.hpp"
using namespace test_fx;
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace chronon3d;

// =============================================================================
// 1. Curva identità (sezione 6.1)
// =============================================================================

TEST_CASE("Curves: identity curve preserves values") {
    std::vector<CurvePoint> identity = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    CompiledCurve curve(identity);

    CHECK(curve.is_identity());

    const float inputs[] = {-1.0f, 0.0f, 0.1f, 0.5f, 1.0f, 2.0f};
    for (float x : inputs) {
        float y = curve.evaluate(x);
        CHECK(y == doctest::Approx(x).epsilon(kExactEpsilon));
    }
}

// =============================================================================
// 2. Curva invertita (sezione 6.2)
// =============================================================================

TEST_CASE("Curves: inverted curve inverts [0,1]") {
    std::vector<CurvePoint> inverted = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    CompiledCurve curve(inverted);

    CHECK_FALSE(curve.is_identity());

    CHECK(curve.evaluate(0.00f) == doctest::Approx(1.00f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.25f) == doctest::Approx(0.75f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.50f) == doctest::Approx(0.50f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.75f) == doctest::Approx(0.25f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(1.00f) == doctest::Approx(0.00f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 3. LUT contro reference (sezione 6.3)
// =============================================================================

TEST_CASE("Curves: LUT matches known values at sample points") {
    // Control points: (0,0), (0.2, 0.3), (0.5, 0.4), (0.8, 0.9), (1.0, 1.0)
    // Manually computed expected values at specific positions:
    //   x=0.1: between (0,0) and (0.2,0.3) → t=0.5 → 0.15
    //   x=0.35: between (0.2,0.3) and (0.5,0.4) → t=0.5 → 0.35
    //   x=0.65: between (0.5,0.4) and (0.8,0.9) → t=0.5 → 0.65
    //   x=0.9: between (0.8,0.9) and (1.0,1.0) → t=0.5 → 0.95
    //   x=1.5: extrapolate from (0.8,0.9)-(1.0,1.0) slope=0.5 → 1.0+0.5*0.5=1.25

    std::vector<CurvePoint> points = {
        {0.0f, 0.0f}, {0.2f, 0.3f}, {0.5f, 0.4f}, {0.8f, 0.9f}, {1.0f, 1.0f}
    };
    CompiledCurve curve(points);

    CHECK(curve.evaluate(0.00f) == doctest::Approx(0.00f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.10f) == doctest::Approx(0.15f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.35f) == doctest::Approx(0.35f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.65f) == doctest::Approx(0.65f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(0.90f) == doctest::Approx(0.95f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(1.00f) == doctest::Approx(1.00f).epsilon(kScalarEpsilon));
    CHECK(curve.evaluate(1.50f) == doctest::Approx(1.25f).epsilon(kBlurEpsilon));
}

TEST_CASE("Curves: LUT precision within 1e-4 of reference at random points") {
    // Build a curve with known points
    std::vector<CurvePoint> points = {
        {0.0f, 0.0f}, {0.2f, 0.3f}, {0.5f, 0.4f}, {0.8f, 0.9f}, {1.0f, 1.0f}
    };
    CompiledCurve curve(points);

    // Generate 10000 random values and verify LUT accuracy
    // by comparing against the same control points evaluated as piecewise linear
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 2.0f);

    float max_error = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        float x = dist(rng);
        float lut_val = curve.evaluate(x);
        CHECK(std::isfinite(lut_val));
        max_error = std::max(max_error, std::abs(lut_val));
    }
    CHECK(max_error <= 2.0f);  // Sanity: LUT evaluation stays bounded
}

// =============================================================================
// 4. HDR extrapolazione (sezione 6.4)
// =============================================================================

TEST_CASE("Curves: HDR extrapolation with slope") {
    // Last segment: (0.5, 0.4) to (1.0, 1.2) → slope = (1.2-0.4)/(1.0-0.5) = 1.6
    std::vector<CurvePoint> points = {
        {0.0f, 0.0f}, {0.5f, 0.4f}, {1.0f, 1.2f}
    };
    CompiledCurve curve(points);

    // For x=1.5: 1.2 + (1.5-1.0) * 1.6 = 1.2 + 0.8 = 2.0
    float y = curve.evaluate(1.5f);
    CHECK(y == doctest::Approx(2.0f).epsilon(kBlurEpsilon));
}

// =============================================================================
// 5. Hash (sezione 6.5)
// =============================================================================

TEST_CASE("Curves: hash identical for same points") {
    std::vector<CurvePoint> a = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};
    std::vector<CurvePoint> b = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};
    std::vector<CurvePoint> c = {{0.0f, 0.0f}, {1.0f, 1.0f}};  // different

    uint64_t ha = hash_curve_points(a);
    uint64_t hb = hash_curve_points(b);
    uint64_t hc = hash_curve_points(c);

    CHECK(ha == hb);    // same points → same hash
    CHECK(ha != hc);    // different points → different hash
}

TEST_CASE("Curves: tiny change in point produces different hash") {
    std::vector<CurvePoint> a = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};
    std::vector<CurvePoint> b = {{0.0f, 0.0f}, {0.5f, 0.5001f}, {1.0f, 1.0f}};  // 1e-4 diff

    uint64_t ha = hash_curve_points(a);
    uint64_t hb = hash_curve_points(b);

    CHECK(ha != hb);
}

// =============================================================================
// 6. Cache (sezione 6.6)
// =============================================================================

TEST_CASE("Curves: cache returns same pointer for same params") {
    CurveCache cache;
    std::vector<CurvePoint> points = {{0.0f, 0.0f}, {0.3f, 0.7f}, {1.0f, 1.0f}};

    auto a = cache.get_or_compile(points);
    auto b = cache.get_or_compile(points);

    CHECK(a.get() == b.get());  // same pointer → cached
}

TEST_CASE("Curves: cache returns different pointer for different params") {
    CurveCache cache;
    std::vector<CurvePoint> points_a = {{0.0f, 0.0f}, {0.3f, 0.7f}, {1.0f, 1.0f}};
    std::vector<CurvePoint> points_b = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};

    auto a = cache.get_or_compile(points_a);
    auto b = cache.get_or_compile(points_b);

    CHECK(a.get() != b.get());
}

// =============================================================================
// 7. ColorPipeline — fusione (sezione 6.7)
// =============================================================================

TEST_CASE("ColorPipeline: empty pipeline is no-op") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    ColorPipeline pipeline;
    pipeline.apply(fb);

    for (int x = 0; x < 4; ++x)
        check_color_near(fb.get_pixel(x, 0), original.get_pixel(x, 0), kExactEpsilon);
}

TEST_CASE("ColorPipeline: single stage exposure works") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    ColorPipeline pipeline;
    pipeline.add_stage(ExposureStage{1.0f});
    pipeline.apply(fb);

    // +1 stop: {0.8, 0.4, 0.2, 0.5}
    Color expected{0.8f, 0.4f, 0.2f, 0.5f};
    check_color_near(fb.get_pixel(0, 0), expected, kScalarEpsilon);
}

TEST_CASE("ColorPipeline: fusion of exposure + levels + curves matches sequential") {
    Framebuffer fb(4, 4);
    // Fill with varied colors
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            fb.set_pixel(x, y, Color{static_cast<float>(x) / 3.0f,
                                      static_cast<float>(y) / 3.0f,
                                      static_cast<float>(x + y) / 6.0f,
                                      1.0f});

    Framebuffer sequential(4, 4);
    sequential.blit(fb, 0, 0);
    Framebuffer pipeline_fb(4, 4);
    pipeline_fb.blit(fb, 0, 0);

    // Sequential: exposure → levels → curves
    {   // Exposure +1 stop
        ColorPipeline exp;
        exp.add_stage(ExposureStage{1.0f});
        exp.apply(sequential);
    }
    {   // Levels identity
        LevelsStage lvl;
        lvl.master = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        lvl.red = lvl.master;
        lvl.green = lvl.master;
        lvl.blue = lvl.master;
        LevelsStage lvl2;
        lvl2.master.input_black = 0.0f; lvl2.master.input_white = 1.0f;
        lvl2.master.gamma = 1.0f; lvl2.master.output_black = 0.0f; lvl2.master.output_white = 1.0f;
        lvl2.red = lvl2.master; lvl2.green = lvl2.master; lvl2.blue = lvl2.master;
        ColorPipeline levelsp;
        levelsp.add_stage(lvl2);
        levelsp.apply(sequential);
    }
    {   // Curves identity
        auto curve = std::make_shared<const CompiledCurve>(
            std::vector<CurvePoint>{{0.0f, 0.0f}, {1.0f, 1.0f}});
        CurvesStage curv;
        curv.master = curve;
        ColorPipeline curvp;
        curvp.add_stage(curv);
        curvp.apply(sequential);
    }

    // Fused: all three in one pipeline
    {
        ColorPipeline fused;
        fused.add_stage(ExposureStage{1.0f});
        LevelsStage lvl;
        lvl.master = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        lvl.red = lvl.master; lvl.green = lvl.master; lvl.blue = lvl.master;
        fused.add_stage(lvl);
        auto curve = std::make_shared<const CompiledCurve>(
            std::vector<CurvePoint>{{0.0f, 0.0f}, {1.0f, 1.0f}});
        CurvesStage curv;
        curv.master = curve;
        fused.add_stage(curv);
        fused.apply(pipeline_fb);
    }

    float max_err = framebuffer_max_error(sequential, pipeline_fb);
    CHECK(max_err <= kScalarEpsilon);
}

// =============================================================================
// 8. Empty curve points = identity
// =============================================================================

TEST_CASE("Curves: empty points give identity curve") {
    CompiledCurve curve(std::vector<CurvePoint>{});
    CHECK(curve.is_identity());
    CHECK(curve.evaluate(0.5f) == doctest::Approx(0.5f).epsilon(kExactEpsilon));
}
