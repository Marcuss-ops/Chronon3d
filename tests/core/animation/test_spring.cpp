#include <doctest/doctest.h>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/animation/motion/motion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
using namespace chronon3d;


// ── sample_spring — canonical math (TICKET-ANIM-SPRING-UNIFY) ──────────

TEST_CASE("Spring sample_spring — underdamped (default config)") {
    f32 from = 0.0f;
    f32 to   = 100.0f;

    SUBCASE("t=0 returns from exactly") {
        CHECK(sample_spring(0.0, from, to) == doctest::Approx(from));
    }

    SUBCASE("large t converges to to") {
        CHECK(sample_spring(10.0, from, to) == doctest::Approx(to).epsilon(0.01));
    }

    SUBCASE("result is finite — no NaN or Inf") {
        for (TimeSeconds t : {0.0, 0.1, 0.5, 1.0, 2.0, 5.0}) {
            f32 v = sample_spring(t, from, to);
            CHECK(std::isfinite(v));
        }
    }

    SUBCASE("deterministic across identical calls") {
        CHECK(sample_spring(1.5, from, to) == sample_spring(1.5, from, to));
    }

    SUBCASE("from == to returns to immediately") {
        CHECK(sample_spring(0.5, 50.0f, 50.0f) == doctest::Approx(50.0f));
    }
}

TEST_CASE("Spring sample_spring — initial velocity (v0 != 0)") {
    SUBCASE("positive v0 boosts early trajectory toward `to`") {
        // Default config: at-rest start.  With v0 = +500 the trajectory
        // overshoots/accelerates toward `to` faster at small t.
        SpringConfig at_rest{};
        SpringConfig boosted{
            .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
            .initial_velocity = 500.0f};
        f32 rest    = sample_spring(0.05, 0.0f, 100.0f, at_rest);
        f32 boosted_v = sample_spring(0.05, 0.0f, 100.0f, boosted);
        CHECK(!(boosted_v == doctest::Approx(rest)));
        CHECK(boosted_v > rest);
        CHECK(std::isfinite(boosted_v));
    }

    SUBCASE("v0 == 0 matches default-constructed config") {
        SpringConfig explicit_zero{
            .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
            .initial_velocity = 0.0f};
        CHECK(sample_spring(1.5, 0.0f, 100.0f, explicit_zero)
              == doctest::Approx(sample_spring(1.5, 0.0f, 100.0f)));
    }

    SUBCASE("deterministic with v0") {
        SpringConfig with_v0{
            .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
            .initial_velocity = 100.0f};
        CHECK(sample_spring(0.3, 0.0f, 100.0f, with_v0)
              == sample_spring(0.3, 0.0f, 100.0f, with_v0));
    }
}

TEST_CASE("Spring sample_spring exposes deterministic velocity") {
    const SpringConfig config{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
        .initial_velocity = 10.0f};

    const SpringSample at_zero = sample_spring(0.0, 0.0f, 1.0f, config);
    const SpringSample at_time = sample_spring(0.1, 0.0f, 1.0f, config);

    CHECK(at_zero.value == 0.0f);
    CHECK(at_zero.velocity == 10.0f);
    CHECK(std::isfinite(at_time.value));
    CHECK(std::isfinite(at_time.velocity));
    const SpringSample repeated = sample_spring(0.1, 0.0f, 1.0f, config);
    CHECK(at_time.value == repeated.value);
    CHECK(at_time.velocity == repeated.velocity);
}

TEST_CASE("Spring invalid configuration fails clearly") {
    CHECK_THROWS_AS(
        sample_spring(0.1, 0.0f, 1.0f,
                            SpringConfig{.mass = 0.0f}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        sample_spring(0.1, 0.0f, 1.0f,
                            SpringConfig{.stiffness = -1.0f}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        sample_spring(0.1, 0.0f, 1.0f,
                            SpringConfig{.damping = -1.0f}),
        std::invalid_argument);
}

TEST_CASE("Spring sample_spring — overdamped case") {
    // zeta = damping / (2 * sqrt(stiffness * mass))
    // zeta > 1 ⇒ overdamped; no oscillation.
    SpringConfig overdamped{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 50.0f};
    f32 val = sample_spring(5.0, 0.0f, 100.0f, overdamped);
    CHECK(val == doctest::Approx(100.0f).epsilon(0.01));
    CHECK(std::isfinite(val));
}

TEST_CASE("Spring sample_spring — critically damped case") {
    // zeta = 1 when damping = 2 * sqrt(stiffness * mass).
    SpringConfig critical{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 20.0f};
    f32 val = sample_spring(5.0, 0.0f, 100.0f, critical);
    CHECK(val == doctest::Approx(100.0f).epsilon(0.01));
    CHECK(std::isfinite(val));
}

// ── Frame-aware overloads (thin wrappers around sample_spring) ─────────

TEST_CASE("Spring — FrameContext overload") {
    FrameContext ctx;
    ctx = ctx.with_frame(Frame{0});
    ctx = ctx.with_frame_rate({30, 1});

    SUBCASE("frame 0 returns from") {
        CHECK(spring(ctx, 0.0f, 100.0f) == doctest::Approx(0.0f));
    }

    SUBCASE("high frame converges") {
        ctx = ctx.with_frame(Frame{300});
        CHECK(spring(ctx, 0.0f, 100.0f) == doctest::Approx(100.0f).epsilon(0.01));
    }
}

TEST_CASE("Spring — higher damping converges faster") {
    f32 from = 0.0f, to = 100.0f;
    SpringConfig low{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 5.0f};
    SpringConfig high{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 40.0f};

    f32 val_low  = std::abs(sample_spring(2.0, from, to, low)  - to);
    f32 val_high = std::abs(sample_spring(2.0, from, to, high) - to);
    CHECK(val_high < val_low);
}

TEST_CASE("Spring — Spring::Gentle/Snappy presets construct canonical config") {
    // Sanity: presets use the canonical (mass, stiffness, damping, v0)
    // field order. Test compile-time access to make sure no break in
    // brace-init contract.
    static_assert(Spring::Gentle.mass == 1.0f);
    static_assert(Spring::Snappy.stiffness == 200.0f);
    static_assert(Spring::Bouncy.damping == 12.0f);
    static_assert(Spring::Heavy.initial_velocity == 0.0f);
    CHECK(true); // Reaching here means compile-time checks passed.
}

// spring random-access parity — pre-refactor behavior lock
//
// Locks the canonical sample_spring + SpringConfig contract BEFORE any
// future spring physics refactor (Fase 2/3 of the 12-commit plan). Each
// SUBCASE targets a single observable property:
//   (a) sample_spring is a pure function with no hidden state;
//       resampling the same args in any order yields bit-identical values;
//   (b) spring(Frame, FrameRate, ...) maps (Frame, FrameRate) to the same
//       elapsed time when the ratios match (e.g. Frame{30}@30fps == Frame{60}@60fps);
//   (c) initial_velocity non-zero creates a non-zero early-time slope
//       distinguishing forward vs backward initial pushes;
//   (d) MotionTimeline::spring bake = direct sample_spring at the corresponding
//       elapsed time (locks the kSpringBakeFps=60 fps invariant for the bake path).

TEST_CASE("spring random-access parity") {
    SpringConfig cfg{
        .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
        .initial_velocity = 0.0f};

    SUBCASE("random frame order produces identical samples") {
        // Locks: sample_spring is a pure function — sampling the same
        // (frame, config) in any order must yield bit-identical results
        // to the declared-order sample. First sample in declared order
        // {0, 30, 5, 60, 15, 1}, then sample in REVERSED order, asserting
        // per-frame identity against the captured values. The reversed
        // pass proves sample_spring has no order-dependence beyond
        // argument transcription (no accumulator state, no globals).
        const std::array<Frame, 6> frames{
            Frame{0}, Frame{30}, Frame{5}, Frame{60}, Frame{15}, Frame{1}};
        std::array<f32, 6> seq_results{};
        for (size_t i = 0; i < frames.size(); ++i) {
            const f32 t = static_cast<f32>(frames[i].integral()) / 30.0f;
            seq_results[i] = sample_spring(t, 0.0f, 1.0f, cfg);
        }
        for (size_t i = 0; i < frames.size(); ++i) {
            const size_t j = frames.size() - 1 - i;  // reversed index
            const f32 t = static_cast<f32>(frames[j].integral()) / 30.0f;
            CHECK(sample_spring(t, 0.0f, 1.0f, cfg)
                  == doctest::Approx(seq_results[j]));
        }
    }

    SUBCASE("Frame{30}@30fps == Frame{60}@60fps (both = 1 second)") {
        // Both expressions evaluate to elapsed = 1 second through
        // FrameRate::to_seconds; spring() Framework overloads MUST yield
        // identical values for mathematically-equivalent (Frame, FrameRate)
        // pairs across different frame rates.
        const f32 at_30fps =
            spring(Frame{30}, FrameRate{30, 1}, 0.0f, 1.0f, cfg);
        const f32 at_60fps =
            spring(Frame{60}, FrameRate{60, 1}, 0.0f, 1.0f, cfg);
        CHECK(at_30fps == doctest::Approx(at_60fps));
    }

    SUBCASE("initial_velocity forward vs backward differs at Frame{1}") {
        // Locks: positive initial_velocity pushes the early trajectory
        // toward `to`; negative pulls it away. By Frame{1} at 30fps
        // (≈ 0.0333 s) the difference is large enough that the comparison
        // is robust without tolerance games.
        SpringConfig forward{
            .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
            .initial_velocity = 10.0f};
        SpringConfig backward{
            .mass = 1.0f, .stiffness = 100.0f, .damping = 15.0f,
            .initial_velocity = -10.0f};
        const f32 rest_fwd =
            sample_spring(1.0f / 30.0f, 0.0f, 1.0f, forward);
        const f32 rest_bwd =
            sample_spring(1.0f / 30.0f, 0.0f, 1.0f, backward);
        CHECK(rest_fwd > rest_bwd);
    }

    SUBCASE("MotionTimeline::spring bake == direct sample_spring at Frame{20}") {
        // MotionTimeline<T>::spring(Frame duration, T target, SpringConfig)
        // bakes sample_spring at kSpringBakeFps = 60 fps into 1-frame
        // keyframes via push_back in motion.hpp:173-181. At Frame{20}
        // from the bake start (which begins at current_frame() = 0 since
        // the timeline starts at frame 0), elapsed = 20/60 s and the
        // baked keyframe at Frame{20} MUST equal the direct sample_spring
        // call at the same elapsed time.
        const f32 from = 0.0f;
        const f32 to   = 1.0f;

        auto prog = MotionTimeline<f32>(from)
            .spring(Frame{20}, to, cfg)
            .compile();

        // prog.keyframes contains entries at Frame{0} (initial) plus one
        // per baked frame (21 entries for a Frame{20} spring on a fresh
        // timeline). Locate the entry at Frame{20} by integral value.
        auto it = std::find_if(
            prog.keyframes.begin(),
            prog.keyframes.end(),
            [](const Keyframe<f32>& kf) {
                return kf.frame.integral() == 20;
            });
        REQUIRE(it != prog.keyframes.end());
        const f32 baked = static_cast<f32>(it->value);

        // MotionTimeline bakes at 60 fps: at f=20 from start, elapsed = 20/60.
        const f32 expected =
            sample_spring(20.0f / 60.0f, from, to, cfg);
        CHECK(baked == doctest::Approx(expected));
    }
}
