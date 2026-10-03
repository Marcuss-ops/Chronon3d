// test_temporal_curve_1d.cpp — live 1D timing curve coverage.
#include <doctest/doctest.h>
#include <chronon3d/animation/core/temporal_curve_1d.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <vector>
using namespace chronon3d;

// =========================================================================
// TemporalCurve1D — timing curve
// =========================================================================

TEST_CASE("TemporalCurve1D: linear is identity") {
    TemporalCurve1D curve(EasingCurve{Easing::Linear});
    // Linear easing is trivial (passthrough), but is_non_trivial() may be true
    // if the easing is anything other than the default Linear.
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.5f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("TemporalCurve1D: easing modifies progress") {
    TemporalCurve1D curve(EasingCurve{Easing::InOutCubic});
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));
    // At t=0.5, InOutCubic should still be ~0.5 (symmetric).
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.5f).epsilon(0.01f));
    // At t=0.25, InOutCubic is < 0.25 (slow start).
    CHECK(curve.evaluate_normalized(0.25f) < 0.25f);
}

TEST_CASE("TemporalCurve1D: hold freezes at 0") {
    TemporalCurve1D curve(EasingCurve{Easing::Hold});
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("TemporalCurve1D: clamps input") {
    TemporalCurve1D curve(EasingCurve{Easing::Linear});
    CHECK(curve.evaluate_normalized(-0.5f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.5f) == doctest::Approx(1.0f));
}

TEST_CASE("TemporalCurve1D: from AnimatedValue<f32> with keyframes") {
    AnimatedValue<f32> av(0.0f);
    // Map t ∈ [0,1] to values [0,100] with temporal bezier handles.
    av.add_keyframe(Frame{0}, 0.0f);
    av.add_keyframe(Frame{100}, 100.0f, InterpMode::Bezier,
                    0.0f, 0.0f, 20.0f, 30.0f);  // out: dx=20, dy=30

    // Update the first keyframe's out-tangent via keyframes() access
    auto& kfs = const_cast<std::vector<Keyframe<f32>>&>(av.keyframes());
    kfs[0].interp = InterpMode::Bezier;
    kfs[0].temporal_out_dx = 0.0f;
    kfs[0].temporal_out_dy = 30.0f;

    TemporalCurve1D curve(std::move(av));
    CHECK(curve.has_animation_curve());
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));

    // At t=0.5, with bezier handles, progress ≠ 0.5.
    f32 mid = curve.evaluate_normalized(0.5f);
    CHECK(mid >= 0.0f);
    CHECK(mid <= 1.0f);
}
