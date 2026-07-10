// ---------------------------------------------------------------------------
// test_frame_rate_edge_cases.cpp — P2-D: Frame rate edge case coverage.
//
// Tests core types only (FrameRate, SampleTime, TemporalSampleKey).
// No video/media dependency — builds on every preset.
//
// Tests:
//   Section A: FrameRate construction for all standard broadcast rates
//   Section B: fps() precision for rational (non-integer) rates
//   Section C: to_seconds() / to_frame() round-trip for each rate
//   Section D: SampleTime sub-frame evaluation (0.0, 0.25, 0.5, 0.999)
//   Section E: Freeze frame — repeated SampleTime produces identical output
//   Section F: Reverse playback — decreasing frame numbers evaluate correctly
//   Section H: Edge cases — very high fps, very low fps, extreme ratios
//   Section J: SampleTime::from_seconds round-trip
//   Section K: Subframe tick precision at non-standard rates
// ---------------------------------------------------------------------------

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/time.hpp>

#include <cmath>

using namespace chronon3d;

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────

/// Tolerance for float comparisons — frame-rate arithmetic has inherent
/// rounding from the int→double conversion.
constexpr double kFpsEps = 1e-6;

/// Tolerance for time-in-seconds comparisons (accumulated rounding).
constexpr double kSecondsEps = 1e-6;

/// Check that two doubles are approximately equal.
[[nodiscard]] bool approx(double a, double b, double eps = kFpsEps) {
    return std::abs(a - b) < eps;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════
//  Section A: FrameRate construction for all standard broadcast rates
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("FrameRate: 23.976 fps (24000/1001) constructs correctly") {
    FrameRate r{24000, 1001};
    CHECK(r.numerator == 24000);
    CHECK(r.denominator == 1001);
    CHECK(approx(r.fps(), 23.976023976, 1e-3));
}

TEST_CASE("FrameRate: 24 fps constructs correctly") {
    FrameRate r{24, 1};
    CHECK(r.numerator == 24);
    CHECK(r.denominator == 1);
    CHECK(approx(r.fps(), 24.0));
}

TEST_CASE("FrameRate: 25 fps constructs correctly") {
    FrameRate r{25, 1};
    CHECK(r.numerator == 25);
    CHECK(r.denominator == 1);
    CHECK(approx(r.fps(), 25.0));
}

TEST_CASE("FrameRate: 29.97 fps (30000/1001) constructs correctly") {
    FrameRate r{30000, 1001};
    CHECK(r.numerator == 30000);
    CHECK(r.denominator == 1001);
    CHECK(approx(r.fps(), 29.970029970, 1e-3));
}

TEST_CASE("FrameRate: 30 fps constructs correctly") {
    FrameRate r{30, 1};
    CHECK(r.numerator == 30);
    CHECK(r.denominator == 1);
    CHECK(approx(r.fps(), 30.0));
}

TEST_CASE("FrameRate: 59.94 fps (60000/1001) constructs correctly") {
    FrameRate r{60000, 1001};
    CHECK(r.numerator == 60000);
    CHECK(r.denominator == 1001);
    CHECK(approx(r.fps(), 59.940059940, 1e-3));
}

TEST_CASE("FrameRate: 60 fps constructs correctly") {
    FrameRate r{60, 1};
    CHECK(r.numerator == 60);
    CHECK(r.denominator == 1);
    CHECK(approx(r.fps(), 60.0));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section B: fps() precision for rational (non-integer) rates
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("FrameRate: fps() returns exact rational quotient") {
    FrameRate film{24000, 1001};
    double expected = 24000.0 / 1001.0;
    CHECK(approx(film.fps(), expected, 1e-15));

    FrameRate ntsc{30000, 1001};
    expected = 30000.0 / 1001.0;
    CHECK(approx(ntsc.fps(), expected, 1e-15));

    FrameRate pal{25, 1};
    CHECK(approx(pal.fps(), 25.0, 1e-15));
}

TEST_CASE("FrameRate: num()/den() accessors match numerator/denominator") {
    FrameRate r{24000, 1001};
    CHECK(r.num() == r.numerator);
    CHECK(r.den() == r.denominator);

    FrameRate r2{30, 1};
    CHECK(r2.num() == 30);
    CHECK(r2.den() == 1);
}

// ═════════════════════════════════════════════════════════════════════════
//  Section C: to_seconds() / to_frame() round-trip for each rate
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("FrameRate: to_seconds(to_frame(t)) ≈ t for 23.976 fps") {
    FrameRate r{24000, 1001};
    double secs = 1.0;
    Frame f = r.to_frame(secs);
    double roundtrip = r.to_seconds(f);
    double max_error = 1.0 / r.fps();
    CHECK(std::abs(roundtrip - secs) < max_error + kSecondsEps);
}

TEST_CASE("FrameRate: to_seconds(to_frame(t)) ≈ t for 29.97 fps") {
    FrameRate r{30000, 1001};
    double secs = 2.5;
    Frame f = r.to_frame(secs);
    double roundtrip = r.to_seconds(f);
    double max_error = 1.0 / r.fps();
    CHECK(std::abs(roundtrip - secs) < max_error + kSecondsEps);
}

TEST_CASE("FrameRate: to_seconds(to_frame(t)) ≈ t for 60 fps") {
    FrameRate r{60, 1};
    double secs = 1.0;
    Frame f = r.to_frame(secs);
    double roundtrip = r.to_seconds(f);
    CHECK(approx(roundtrip, secs, kSecondsEps));
}

TEST_CASE("FrameRate: to_frame(0) == 0 for all rates") {
    FrameRate rates[] = {
        {24000, 1001}, {24, 1}, {25, 1}, {30000, 1001}, {30, 1},
        {60000, 1001}, {60, 1}, {120, 1}, {1, 1}
    };
    for (auto r : rates) {
        CHECK(r.to_frame(0.0) == 0);
    }
}

TEST_CASE("FrameRate: to_seconds(0) == 0.0 for all rates") {
    FrameRate rates[] = {
        {24000, 1001}, {24, 1}, {25, 1}, {30000, 1001}, {30, 1},
        {60000, 1001}, {60, 1}
    };
    for (auto r : rates) {
        CHECK(approx(r.to_seconds(0), 0.0, 1e-15));
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Section D: SampleTime sub-frame evaluation
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("SampleTime: subframe 0.0 — integral frame, zero fraction") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.0, r);
    CHECK(st.integral_frame() == 10);
    CHECK(approx(st.fraction(), 0.0, 1e-15));
    CHECK(approx(st.seconds(), 10.0 / 30.0, kSecondsEps));
}

TEST_CASE("SampleTime: subframe 0.25 — quarter frame") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.25, r);
    CHECK(st.integral_frame() == 10);
    CHECK(approx(st.fraction(), 0.25, 1e-10));
    CHECK(approx(st.seconds(), 10.25 / 30.0, kSecondsEps));
}

TEST_CASE("SampleTime: subframe 0.5 — half frame") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.5, r);
    CHECK(st.integral_frame() == 10);
    CHECK(approx(st.fraction(), 0.5, 1e-10));
    CHECK(approx(st.seconds(), 10.5 / 30.0, kSecondsEps));
}

TEST_CASE("SampleTime: subframe 0.999 — near next frame") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.999, r);
    CHECK(st.integral_frame() == 10);
    CHECK(approx(st.fraction(), 0.999, 1e-6));
    CHECK(approx(st.seconds(), 10.999 / 30.0, kSecondsEps));
}

TEST_CASE("SampleTime: subframe at 23.976 fps — fractional frame timing") {
    FrameRate r{24000, 1001};
    auto st = SampleTime::from_frame(47.5, r);
    CHECK(st.integral_frame() == 47);
    CHECK(approx(st.fraction(), 0.5, 1e-10));
    double expected_secs = 47.5 * 1001.0 / 24000.0;
    CHECK(approx(st.seconds(), expected_secs, kSecondsEps));
}

TEST_CASE("SampleTime: subframe at 59.94 fps — high rate fractional") {
    FrameRate r{60000, 1001};
    auto st = SampleTime::from_frame(119.75, r);
    CHECK(st.integral_frame() == 119);
    CHECK(approx(st.fraction(), 0.75, 1e-10));
    double expected_secs = 119.75 * 1001.0 / 60000.0;
    CHECK(approx(st.seconds(), expected_secs, kSecondsEps));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section E: Freeze frame — repeated SampleTime produces identical output
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Freeze frame: same SampleTime yields identical seconds/keys") {
    FrameRate r{30, 1};
    auto st1 = SampleTime::from_frame(5.0, r);
    auto st2 = SampleTime::from_frame(5.0, r);

    CHECK(st1 == st2);
    CHECK(approx(st1.seconds(), st2.seconds(), 1e-15));
    CHECK(st1.integral_frame() == st2.integral_frame());
    CHECK(approx(st1.fraction(), st2.fraction(), 1e-15));
}

TEST_CASE("Freeze frame: TemporalSampleKey identical for same SampleTime") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(42.0, r);
    auto key1 = make_temporal_key(st, 0);
    auto key2 = make_temporal_key(st, 0);
    CHECK(key1 == key2);
}

TEST_CASE("Freeze frame: 1000 repeated frames produce identical keys") {
    FrameRate r{24, 1};
    auto st = SampleTime::from_frame(100.0, r);
    auto ref_key = make_temporal_key(st, 42);
    for (int i = 0; i < 1000; ++i) {
        auto key = make_temporal_key(st, 42);
        CHECK(key == ref_key);
    }
}

TEST_CASE("Freeze frame: subframe freeze at 0.5 repeated identically") {
    FrameRate r{24000, 1001};
    auto st = SampleTime::from_frame(50.5, r);
    auto key1 = make_temporal_key(st, 0);
    auto st2 = SampleTime::from_frame(50.5, r);
    auto key2 = make_temporal_key(st2, 0);
    CHECK(key1 == key2);
    CHECK(approx(st.seconds(), st2.seconds(), 1e-12));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section F: Reverse playback — decreasing frame numbers
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Reverse: decreasing frame numbers produce decreasing seconds") {
    FrameRate r{30, 1};
    double prev_secs = 1e30;
    for (int f = 100; f >= 0; --f) {
        auto st = SampleTime::from_frame(static_cast<double>(f), r);
        CHECK(st.seconds() <= prev_secs);
        prev_secs = st.seconds();
    }
}

TEST_CASE("Reverse: frame 100 → frame 0 at 23.976 fps — monotonic seconds") {
    FrameRate r{24000, 1001};
    double prev_secs = 1e30;
    for (int f = 100; f >= 0; --f) {
        auto st = SampleTime::from_frame(static_cast<double>(f), r);
        CHECK(st.seconds() <= prev_secs + 1e-12);
        prev_secs = st.seconds();
    }
}

TEST_CASE("Reverse: negative frame numbers produce negative seconds") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(-10.0, r);
    CHECK(st.seconds() < 0.0);
    CHECK(approx(st.seconds(), -10.0 / 30.0, kSecondsEps));
    CHECK(st.integral_frame() == static_cast<Frame>(-10));
}

TEST_CASE("Reverse: frame 0 is the boundary — seconds == 0") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(0.0, r);
    CHECK(approx(st.seconds(), 0.0, 1e-15));
    CHECK(st.integral_frame() == 0);
    CHECK(approx(st.fraction(), 0.0, 1e-15));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section H: Edge cases — extreme rates
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("FrameRate: 120 fps — to_seconds/to_frame round-trip") {
    FrameRate r{120, 1};
    double secs = 0.5;
    Frame f = r.to_frame(secs);
    CHECK(f == 60);
    double roundtrip = r.to_seconds(f);
    CHECK(approx(roundtrip, 0.5, kSecondsEps));
}

TEST_CASE("FrameRate: 1 fps — to_seconds/to_frame round-trip") {
    FrameRate r{1, 1};
    double secs = 5.0;
    Frame f = r.to_frame(secs);
    CHECK(f == 5);
    double roundtrip = r.to_seconds(f);
    CHECK(approx(roundtrip, 5.0, kSecondsEps));
}

TEST_CASE("FrameRate: large numerator/denominator (48000/1001 ≈ 47.952 fps)") {
    FrameRate r{48000, 1001};
    CHECK(approx(r.fps(), 48000.0 / 1001.0, 1e-12));
    auto st = SampleTime::from_frame(100.0, r);
    double expected = 100.0 * 1001.0 / 48000.0;
    CHECK(approx(st.seconds(), expected, kSecondsEps));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section J: SampleTime::from_seconds round-trip
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("SampleTime: from_seconds round-trip at 23.976 fps") {
    FrameRate r{24000, 1001};
    double secs = 2.5;
    auto st = SampleTime::from_seconds(secs, r);
    CHECK(approx(st.seconds(), secs, 1e-6));
}

TEST_CASE("SampleTime: from_seconds round-trip at 29.97 fps") {
    FrameRate r{30000, 1001};
    double secs = 1.0;
    auto st = SampleTime::from_seconds(secs, r);
    CHECK(approx(st.seconds(), secs, 1e-6));
}

TEST_CASE("SampleTime: from_seconds round-trip at 60 fps") {
    FrameRate r{60, 1};
    double secs = 0.5;
    auto st = SampleTime::from_seconds(secs, r);
    CHECK(approx(st.seconds(), secs, 1e-6));
}

// ═════════════════════════════════════════════════════════════════════════
//  Section K: Subframe tick precision at non-standard rates
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TemporalSampleKey: subframe ticks differ for 0.0 vs 0.5 at 24000/1001") {
    FrameRate r{24000, 1001};
    auto st0 = SampleTime::from_frame(50.0, r);
    auto st5 = SampleTime::from_frame(50.5, r);
    auto key0 = make_temporal_key(st0, 0);
    auto key5 = make_temporal_key(st5, 0);
    CHECK(key0.frame == key5.frame);
    CHECK(key0.subframe_tick != key5.subframe_tick);
}

TEST_CASE("TemporalSampleKey: subframe tick at 0.999 is near kTicksPerFrame") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.999, r);
    auto key = make_temporal_key(st, 0);
    CHECK(key.frame == 10);
    CHECK(key.subframe_tick > 65000);
    CHECK(key.subframe_tick < TemporalSampleKey::kTicksPerFrame);
}

TEST_CASE("TemporalSampleKey: subframe 0.25 produces exactly kTicksPerFrame/4") {
    FrameRate r{30, 1};
    auto st = SampleTime::from_frame(10.25, r);
    auto key = make_temporal_key(st, 0);
    CHECK(key.frame == 10);
    CHECK(key.subframe_tick == 16384);
}
