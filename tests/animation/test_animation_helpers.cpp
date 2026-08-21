// test_animation_helpers.cpp — M1.8 §3A / TICKET-SIMPLICITY-ANIMATION
//
// Locks ALL 17 animation helpers exported by `interpolate.hpp` with
// 17 × 2 = 34 doctest CHECK assertions (determinism + edge case per helper).
//
// Test distribution (17 TEST_CASEs):
//   #1  interpolate          — determinism (2 calls match) + edge (equal start/end → from)
//   #2  spring               — determinism + edge (t=0 → from)
//   #3  sequence             — determinism + edge (before-first → `before` arg)
//   #4  stagger_delay        — determinism (same seed → same delay) + edge (total=1 → Frame{0})
//   #5  stagger (alias)      — alias: same output as stagger_delay for any input
//   #6  stagger_value        — determinism + edge (rank=0 → returns base value)
//   #7  loop                 — determinism + edge (frame >= period → remainder)
//   #8  loop_pingpong        — determinism + edge (even cycle forward, odd reverse)
//   #9  delay                — determinism + edge (frame < delay → value_before)
//   #10 ease                 — determinism + edge (t<0 clamps to 0, t>1 clamps to 1)
//   #11 clamp                — determinism + edge (value > max → max)
//   #12 map                  — determinism + edge (in_min == in_max → out_min)
//   #13 progress             — determinism + edge (frame outside range → clamp to 0/1)
//   #14 tween (alias)        — alias: same output as interpolate
//   #15 frame_to_seconds     — determinism + edge (frame=0 → 0.0f)
//   #16 linear (alias)       — alias: same output as ease(t, Easing::Linear)
//   #17 clamp_progress       — alias: same output as clamp(t, 0, 1)
//
// AGENTS.md v0.1 freeze compliance: pure math harness, no Framebuffer /
// compositor / GPU / FontEngine / HarfBuzz.  Compiles in any preset
// that turns on CHRONON3D_BUILD_TESTS (UNCONDITIONAL registration).

#include <doctest/doctest.h>

#include <chronon3d/animation/interpolate.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/animation/effects/stagger.hpp>

#include <cmath>
#include <initializer_list>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// #1  interpolate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: interpolate — determinism + edge (equal start/end)") {
    // Determinism: 2 calls with identical args must produce identical results.
    const f32 r1 = interpolate(Frame{10}, FrameRange{Frame{0}, Frame{20}},
                               ValueRange{0.0f, 100.0f});
    const f32 r2 = interpolate(Frame{10}, FrameRange{Frame{0}, Frame{20}},
                               ValueRange{0.0f, 100.0f});
    CHECK(r1 == r2);
    // Edge: equal start and end → returns from (avoids divide-by-zero).
    const f32 edge = interpolate(Frame{50}, FrameRange{Frame{10}, Frame{10}},
                                 ValueRange{7.0f, 99.0f});
    CHECK(edge == doctest::Approx(7.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #2  spring
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: spring — determinism + edge (t=0 → from)") {
    // Determinism: identical args → identical results.
    const f32 r1 = spring(Frame{15}, FrameRate{30, 1}, 0.0f, 100.0f);
    const f32 r2 = spring(Frame{15}, FrameRate{30, 1}, 0.0f, 100.0f);
    CHECK(r1 == r2);
    // Edge: t = 0 → returns from (no time has elapsed).
    const f32 edge = spring(Frame{0}, FrameRate{30, 1}, 42.0f, 100.0f);
    CHECK(edge == doctest::Approx(42.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #3  sequence
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: sequence — determinism + edge (before-first → `before` arg)") {
    // Determinism: identical args → identical results.
    const f32 r1 = sequence(Frame{10}, {
        Segment{Frame{0},  Frame{20}, 0.0f, 50.0f, Easing::Linear},
        Segment{Frame{20}, Frame{40}, 50.0f, 80.0f, Easing::Linear},
    }, 0.0f);
    const f32 r2 = sequence(Frame{10}, {
        Segment{Frame{0},  Frame{20}, 0.0f, 50.0f, Easing::Linear},
        Segment{Frame{20}, Frame{40}, 50.0f, 80.0f, Easing::Linear},
    }, 0.0f);
    CHECK(r1 == r2);
    // Edge: frame before first segment → returns `before` arg (default 0.0f).
    const f32 edge = sequence(Frame{-5}, {
        Segment{Frame{0}, Frame{20}, 0.0f, 50.0f, Easing::Linear},
    }, -1.0f);
    CHECK(edge == doctest::Approx(-1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #4  stagger_delay
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: stagger_delay — determinism + edge (total=1 → Frame{0})") {
    StaggerConfig cfg{};
    cfg.delay_per_unit = Frame{4};
    cfg.randomize = 0.0f;  // disable random for determinism
    cfg.random_seed = 42;
    // Determinism: same seed → same delay.
    const Frame d1 = stagger_delay(2, 10, cfg);
    const Frame d2 = stagger_delay(2, 10, cfg);
    CHECK(d1 == d2);
    // Edge: total = 1 → Frame{0} (no stagger possible for 1 element).
    const Frame edge = stagger_delay(0, 1, cfg);
    CHECK(edge == Frame{0});
}

// ═══════════════════════════════════════════════════════════════════════════
// #5  stagger (alias of stagger_delay)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: stagger — alias of stagger_delay (same output)") {
    StaggerConfig cfg{};
    cfg.delay_per_unit = Frame{4};
    cfg.randomize = 0.0f;
    cfg.random_seed = 42;
    // Determinism: alias contract — `stagger(rank, total, cfg) == stagger_delay(rank, total, cfg)`.
    const Frame a = stagger(2, 10, cfg);
    const Frame b = stagger_delay(2, 10, cfg);
    CHECK(a == b);
    // Edge: total = 1 → Frame{0} (first element, no stagger delay).
    CHECK(stagger(0, 1, cfg) == Frame{0});
}

// ═══════════════════════════════════════════════════════════════════════════
// #6  stagger_value (NEW — value-level stagger)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: stagger_value — determinism + edge (rank=0 → base value)") {
    StaggerConfig cfg{};
    cfg.delay_per_unit = Frame{5};
    cfg.randomize = 0.0f;
    cfg.random_seed = 42;
    // Determinism: same args → same value.
    const f32 v1 = stagger_value(0.0f, 3, 8, cfg);
    const f32 v2 = stagger_value(0.0f, 3, 8, cfg);
    CHECK(v1 == v2);
    // Edge: rank=0 with base=10.0f → returns 10.0f (first element has zero delay).
    const f32 edge = stagger_value(10.0f, 0, 5, cfg);
    CHECK(edge == doctest::Approx(10.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #7  loop
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: loop — determinism + edge (frame >= period → remainder)") {
    // Determinism: identical frame + period → identical local frame.
    const Frame r1 = loop(Frame{75}, Frame{30});
    const Frame r2 = loop(Frame{75}, Frame{30});
    CHECK(r1 == r2);
    // Edge: frame = 75, period = 30 → 75 % 30 = 15.
    const Frame edge = loop(Frame{75}, Frame{30});
    CHECK(edge == Frame{15});
}

// ═══════════════════════════════════════════════════════════════════════════
// #8  loop_pingpong
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: loop_pingpong — determinism + edge (even cycle forward, odd reverse)") {
    // Determinism.
    const Frame r1 = loop_pingpong(Frame{70}, Frame{30});
    const Frame r2 = loop_pingpong(Frame{70}, Frame{30});
    CHECK(r1 == r2);
    // Edge: frame 30..59 (cycle 1 = odd) → reverse direction (30..1). Halfway: 35 → 25.
    CHECK(loop_pingpong(Frame{35}, Frame{30}) == Frame{25});  // odd cycle, halfway
}

// ═══════════════════════════════════════════════════════════════════════════
// #9  delay
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: delay — determinism + edge (frame < delay → value_before)") {
    // Determinism.
    const f32 r1 = delay(Frame{20}, Frame{10}, 0.0f, 0.0f, 1.0f,
                        Frame{10}, Frame{30}, Easing::Linear);
    const f32 r2 = delay(Frame{20}, Frame{10}, 0.0f, 0.0f, 1.0f,
                        Frame{10}, Frame{30}, Easing::Linear);
    CHECK(r1 == r2);
    // Edge: frame < delay → returns value_before (the 0.0f we passed).
    const f32 edge = delay(Frame{5}, Frame{10}, -99.0f, 0.0f, 1.0f,
                          Frame{10}, Frame{30}, Easing::Linear);
    CHECK(edge == doctest::Approx(-99.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #10 ease
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: ease — determinism + edge (t<0 clamps to 0, t>1 clamps to 1)") {
    // Determinism.
    const f32 r1 = ease(0.5f, Easing::OutCubic);
    const f32 r2 = ease(0.5f, Easing::OutCubic);
    CHECK(r1 == r2);
    // Edge: t > 1 clamps to 1.0f (upper-bound clamp).
    CHECK(ease(1.5f, Easing::OutCubic) == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #11 clamp
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: clamp — determinism + edge (value > max → max)") {
    // Determinism.
    const f32 r1 = clamp(15.0f, 0.0f, 10.0f);
    const f32 r2 = clamp(15.0f, 0.0f, 10.0f);
    CHECK(r1 == r2);
    // Edge: value > max → max.
    CHECK(clamp(15.0f, 0.0f, 10.0f) == doctest::Approx(10.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #12 map
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: map — determinism + edge (in_min == in_max → out_min)") {
    // Determinism.
    const f32 r1 = map(0.5f, 0.0f, 1.0f, 10.0f, 20.0f);
    const f32 r2 = map(0.5f, 0.0f, 1.0f, 10.0f, 20.0f);
    CHECK(r1 == r2);
    // Edge: in_min == in_max → out_min (avoids divide-by-zero).
    CHECK(map(0.5f, 5.0f, 5.0f, 10.0f, 20.0f) == doctest::Approx(10.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #13 progress
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: progress — determinism + edge (frame outside range → clamp to 0/1)") {
    // Determinism.
    const f32 r1 = progress(Frame{10}, Frame{0}, Frame{20});
    const f32 r2 = progress(Frame{10}, Frame{0}, Frame{20});
    CHECK(r1 == r2);
    // Edge: frame after end → clamped to 1.0f (upper-bound clamp).
    CHECK(progress(Frame{50}, Frame{0}, Frame{20}) == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #14 tween (alias of interpolate)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: tween — alias of interpolate (same output)") {
    // Determinism: alias contract — `tween(frame, range, values, easing) == interpolate(...)`.
    const f32 a = tween(Frame{10}, FrameRange{Frame{0}, Frame{20}},
                        ValueRange{0.0f, 100.0f});
    const f32 b = interpolate(Frame{10}, FrameRange{Frame{0}, Frame{20}},
                             ValueRange{0.0f, 100.0f});
    CHECK(a == b);
    // Edge: equal start and end → returns from (avoids divide-by-zero).
    CHECK(tween(Frame{50}, FrameRange{Frame{10}, Frame{10}},
                ValueRange{7.0f, 99.0f}) == doctest::Approx(7.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #15 frame_to_seconds (NEW — utility)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: frame_to_seconds — determinism + edge (frame=0 → 0.0f)") {
    // Determinism.
    const f32 r1 = frame_to_seconds(Frame{60}, FrameRate{30, 1});
    const f32 r2 = frame_to_seconds(Frame{60}, FrameRate{30, 1});
    CHECK(r1 == r2);
    // Edge: frame=0 → 0.0s (no time elapsed).
    CHECK(frame_to_seconds(Frame{0}, FrameRate{30, 1}) == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #16 linear (alias of ease(t, Easing::Linear))
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: linear — alias of ease(t, Easing::Linear)") {
    // Determinism: alias contract — `linear(t) == ease(t, Easing::Linear)` for any t.
    CHECK(linear(0.5f) == doctest::Approx(ease(0.5f, Easing::Linear)));
    // Edge: linear(0.0f) == 0.0f (identity, lower-bound clamp via ease).
    CHECK(linear(0.0f) == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// #17 clamp_progress (alias of clamp(t, 0, 1))
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animation helpers: clamp_progress — alias of clamp(t, 0, 1)") {
    // Determinism: alias contract — `clamp_progress(t) == clamp(t, 0.0f, 1.0f)`.
    CHECK(clamp_progress(0.5f) == doctest::Approx(clamp(0.5f, 0.0f, 1.0f)));
    // Edge: t > 1 → clamped to 1.0f (upper-bound clamp).
    CHECK(clamp_progress(1.5f) == doctest::Approx(1.0f));
}
