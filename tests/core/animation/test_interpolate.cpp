#include <doctest/doctest.h>
#include <chronon3d/animation/easing/interpolate.hpp>
using namespace chronon3d;


TEST_CASE("interpolate basics") {
    SUBCASE("Linear interpolation at midpoint") {
        CHECK(interpolate(50.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    }

    SUBCASE("Lower bound clamping") {
        CHECK(interpolate(-10.0f, 0.0f, 100.0f, 0.0f, 1.0f) == 0.0f);
    }

    SUBCASE("Upper bound clamping") {
        CHECK(interpolate(120.0f, 0.0f, 100.0f, 0.0f, 1.0f) == 1.0f);
    }

    SUBCASE("Equal start and end points") {
        CHECK(interpolate(50.0f, 10.0f, 10.0f, 5.0f, 10.0f) == 5.0f);
    }

    SUBCASE("Frame overload") {
        CHECK(interpolate(static_cast<Frame>(50), static_cast<Frame>(0), static_cast<Frame>(100), 0.0f, 1.0f) == doctest::Approx(0.5f));
    }
}

// interpolate baseline lock — see TICKET-EXTRAPOLATE-ENUM bundle
//
// Behavior contract locked BEFORE any Extrapolate {Clamp, Extend, Wrap}
// introduction (Fase 1). Each SUBCASE targets a single observable behavior
// of the current ClampMode{Clamp}-only implementation. All assertions must
// PASS bit-identical against the codebase at landing, and remain valid
// through the ClampMode → Extrapolate migration (which must be a
// backward-compatible adapter per the migration plan).
//
// Rationale: the upcoming Extrapolate::Wrap + Extrapolate::Extend
// variants change semantics outside [0,1] range. This lock guarantees
// that the in-[0,1] behavior (linearity + EasingCurve::apply) and the
// degenerate-range edge case remain stable across the migration.

TEST_CASE("interpolate baseline lock") {
    SUBCASE("Linear interpolation at multiple points") {
        // Locks: at t in {0, 0.25, 0.5, 0.75, 1} with linear easing,
        // interpolate(x, 0, 100, 0, 1) == x/100 (within Approx tolerance).
        CHECK(interpolate(0.0f,  0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
        CHECK(interpolate(25.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.25f));
        CHECK(interpolate(50.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.5f));
        CHECK(interpolate(75.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.75f));
        CHECK(interpolate(100.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    }

    SUBCASE("Strict clamp before range (ClampMode default)") {
        // Locks: t < 0 → t clamped to 0 → output_start, for several
        // negative inputs including extreme value. NO Extend or Wrap.
        CHECK(interpolate(-1.0f,    0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
        CHECK(interpolate(-1000.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    }

    SUBCASE("Strict clamp after range (ClampMode default)") {
        // Locks: t > 1 → t clamped to 1 → output_end for several positive
        // over-shoots including extreme value.
        CHECK(interpolate(101.0f,  0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
        CHECK(interpolate(1000.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    }

    SUBCASE("Range degenerate handling") {
        // Locks: when input_start == input_end the function MUST short-
        // circuit to output_start REGARDLESS of the input value, per the
        // implementation contract at interpolate.hpp:30-32. This holds
        // even with arbitrary negative or zero-range inputs.
        CHECK(interpolate(50.0f,   100.0f, 100.0f, 5.0f,  10.0f) == doctest::Approx(5.0f));
        CHECK(interpolate(-99.0f,  0.0f,   0.0f,   3.7f, 9.2f)  == doctest::Approx(3.7f));
        CHECK(interpolate(0.0f,    0.0f,   0.0f,   0.0f, 1.0f)  == doctest::Approx(0.0f));
    }

    SUBCASE("Output range scale independence") {
        // Locks: output range scaling (here {-10, +10} instead of {0, 1})
        // does NOT affect linearity, clamping, or degenerate handling.
        // The function must produce the same t * scale result regardless
        // of the absolute output range.
        CHECK(interpolate(0.0f,   0.0f, 100.0f, -10.0f, 10.0f) == doctest::Approx(-10.0f));
        CHECK(interpolate(50.0f,  0.0f, 100.0f, -10.0f, 10.0f) == doctest::Approx(0.0f));
        CHECK(interpolate(100.0f, 0.0f, 100.0f, -10.0f, 10.0f) == doctest::Approx(10.0f));
    }

    SUBCASE("EasingCurve::OutCubic applied inside range") {
        // Locks: glm::cubicEaseOut(t) = 1 - (1 - t)^3 evaluated at
        // t in {0, 0.5, 1}. Pinpoint values are exact in IEEE-754:
        //   t=0   → 1 - 1^3       = 0
        //   t=0.5 → 1 - 0.5^3     = 1 - 0.125 = 0.875
        //   t=1   → 1 - 0^3       = 1
        CHECK(interpolate(0.0f,   0.0f, 100.0f, 0.0f, 1.0f, EasingCurve{Easing::OutCubic}) == doctest::Approx(0.0f));
        CHECK(interpolate(50.0f,  0.0f, 100.0f, 0.0f, 1.0f, EasingCurve{Easing::OutCubic}) == doctest::Approx(0.875f));
        CHECK(interpolate(100.0f, 0.0f, 100.0f, 0.0f, 1.0f, EasingCurve{Easing::OutCubic}) == doctest::Approx(1.0f));
    }
}
