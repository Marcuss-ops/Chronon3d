#include <doctest/doctest.h>
#include <chronon3d/animation/easing/spring.hpp>
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

TEST_CASE("Spring — SequenceContext overload") {
    FrameContext parent;
    parent = parent.with_frame(Frame{10});
    parent = parent.with_frame_rate({30, 1});

    SequenceContext seq = sequence(parent, Frame{0}, Frame{60});

    SUBCASE("frame 0 in sequence returns from") {
        parent = parent.with_frame(Frame{0});
        seq = sequence(parent, Frame{0}, Frame{60});
        CHECK(spring(seq, 0.0f, 100.0f) == doctest::Approx(0.0f));
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
