#include <doctest/doctest.h>
#include <chronon3d/animation/spring.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("Spring — underdamped (default config)") {
    f32 from = 0.0f;
    f32 to = 100.0f;

    SUBCASE("t=0 returns from exactly") {
        CHECK(spring(0.0f, from, to) == doctest::Approx(from));
    }

    SUBCASE("large t converges to to") {
        CHECK(spring(10.0f, from, to) == doctest::Approx(to).epsilon(0.01));
    }

    SUBCASE("result is finite — no NaN or Inf") {
        for (f32 t : {0.0f, 0.1f, 0.5f, 1.0f, 2.0f, 5.0f}) {
            f32 v = spring(t, from, to);
            CHECK(std::isfinite(v));
        }
    }

    SUBCASE("deterministic across identical calls") {
        CHECK(spring(1.5f, from, to) == spring(1.5f, from, to));
    }

    SUBCASE("from == to returns to immediately") {
        CHECK(spring(0.5f, 50.0f, 50.0f) == doctest::Approx(50.0f));
    }
}

TEST_CASE("Spring — overdamped case") {
    SpringConfig overdamped{.stiffness = 100.0f, .damping = 50.0f, .mass = 1.0f};
    f32 val = spring(5.0f, 0.0f, 100.0f, overdamped);
    CHECK(val == doctest::Approx(100.0f).epsilon(0.01));
    CHECK(std::isfinite(val));
}

TEST_CASE("Spring — critically damped case") {
    // zeta = 1 when damping = 2 * sqrt(stiffness * mass)
    SpringConfig critical{.stiffness = 100.0f, .damping = 20.0f, .mass = 1.0f};
    f32 val = spring(5.0f, 0.0f, 100.0f, critical);
    CHECK(val == doctest::Approx(100.0f).epsilon(0.01));
    CHECK(std::isfinite(val));
}

TEST_CASE("Spring — FrameContext overload") {
    FrameContext ctx;
    ctx.frame = Frame{0};
    ctx.frame_rate = {30, 1};

    SUBCASE("frame 0 returns from") {
        CHECK(spring(ctx, 0.0f, 100.0f) == doctest::Approx(0.0f));
    }

    SUBCASE("high frame converges") {
        ctx.frame = Frame{300};
        CHECK(spring(ctx, 0.0f, 100.0f) == doctest::Approx(100.0f).epsilon(0.01));
    }
}

TEST_CASE("Spring — SequenceContext overload") {
    FrameContext parent;
    parent.frame = Frame{10};
    parent.frame_rate = {30, 1};

    SequenceContext seq = sequence(parent, Frame{0}, Frame{60});

    SUBCASE("frame 0 in sequence returns from") {
        parent.frame = Frame{0};
        seq = sequence(parent, Frame{0}, Frame{60});
        CHECK(spring(seq, 0.0f, 100.0f) == doctest::Approx(0.0f));
    }
}

TEST_CASE("Spring — higher damping converges faster") {
    f32 from = 0.0f, to = 100.0f;
    SpringConfig low{.stiffness = 100.0f, .damping = 5.0f};
    SpringConfig high{.stiffness = 100.0f, .damping = 40.0f};

    f32 val_low  = std::abs(spring(2.0f, from, to, low)  - to);
    f32 val_high = std::abs(spring(2.0f, from, to, high) - to);
    CHECK(val_high < val_low);
}
