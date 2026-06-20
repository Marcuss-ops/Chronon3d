#include <doctest/doctest.h>

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/types.hpp>

// keyframes v2 — direct free-function entry point for keyframe interpolation.
//
// Public API surface:
//   chronon3d::keyframes(frame, {KF, KF, ...})      -> f32   (legacy 2-arg form)
//   chronon3d::keyframes<T>({KF, KF, ...})           -> AnimatedValue<T>  (modern factory)
//
// test_keyframe_tracks.cpp already covers the modern-factory path
// (`keyframes<T>({...})`) exhaustively: basic linear, Hold easing, Vec3,
// Color, fluid-builder, empty-track, and a single legacy-form sanity check.
// This file targets the LEGACY 2-arg form exclusively — the surface NOT
// covered by the sibling — specifically:
//   * interpolation correctness at the midpoint (sanity check vs sibling)
//   * clamping OUTSIDE the first/last keyframe range
//   * Hold-easing behaviour at the keyframe boundary through the legacy path
//
// See TICKET-005 Gap A in docs/FOLLOWUP_TICKETS.md. Previously this file
// held a `DISABLED_keyframes_v2: placeholder ...` no-op while the legacy
// function was absent; the function now exists in
// include/chronon3d/animation/core/animated_value.hpp (line 819+).

using namespace chronon3d;

// Explicit alias so the call site is unambiguous about which overload is
// selected (`std::initializer_list<KF>` vs the templated factory).
using KFList = std::initializer_list<KF>;

TEST_CASE("keyframes legacy: 2-arg form returns f32 with linear interpolation") {
    KFList kfs = { KF{0, 0.0f}, KF{60, 100.0f} };
    CHECK(keyframes(30, kfs) == doctest::Approx(50.0f));
}

TEST_CASE("keyframes legacy: 2-arg form clamps before first and after last keyframe") {
    KFList kfs = { KF{0, 0.0f}, KF{60, 100.0f} };
    CHECK(keyframes(-10, kfs) == doctest::Approx(0.0f));
    CHECK(keyframes(  0, kfs) == doctest::Approx(0.0f));
    CHECK(keyframes( 60, kfs) == doctest::Approx(100.0f));
    CHECK(keyframes( 70, kfs) == doctest::Approx(100.0f));
    CHECK(keyframes(120, kfs) == doctest::Approx(100.0f));
}

TEST_CASE("keyframes legacy: Hold easing steps at keyframe boundary") {
    KFList kfs = {
        KF{ 0, 10.0f, Easing::Hold},
        KF{30, 20.0f, Easing::Hold},
        KF{60, 30.0f, Easing::Hold},
    };
    CHECK(keyframes(0,  kfs) == doctest::Approx(10.0f));
    CHECK(keyframes(15, kfs) == doctest::Approx(10.0f));
    CHECK(keyframes(29, kfs) == doctest::Approx(10.0f));
    CHECK(keyframes(30, kfs) == doctest::Approx(20.0f));
    CHECK(keyframes(45, kfs) == doctest::Approx(20.0f));
    CHECK(keyframes(59, kfs) == doctest::Approx(20.0f));
    CHECK(keyframes(60, kfs) == doctest::Approx(30.0f));
}
