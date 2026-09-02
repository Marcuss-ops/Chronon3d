#include <doctest/doctest.h>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>

using namespace chronon3d;

TEST_CASE("FrameContext basics") {
    const SampleTime global = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1});
    const FrameContext ctx = make_frame_context({
        .global_time = global,
        .duration = Frame{100},
        .width = 1920,
        .height = 1080,
    });

    SUBCASE("Temporal accessors") {
        CHECK(ctx.global_time() == global);
        CHECK(ctx.local_time() == global);
        CHECK(ctx.frame() == Frame{30});
        CHECK(ctx.frame_fraction() == doctest::Approx(0.0));
        CHECK(ctx.frame_rate() == FrameRate{30, 1});
    }

    SUBCASE("Time conversion") {
        CHECK(ctx.seconds() == doctest::Approx(1.0));
    }

    SUBCASE("Progress calculation") {
        CHECK(ctx.progress() == doctest::Approx(0.3));
    }

    SUBCASE("Boundary checks") {
        CHECK_FALSE(ctx.is_first_frame());
        CHECK_FALSE(ctx.is_last_frame());
    }
}

TEST_CASE("FrameContext local vs global time") {
    const SampleTime global = SampleTime::from_frame_int(Frame{50}, FrameRate{24, 1});
    const SampleTime local = SampleTime::from_frame_int(Frame{10}, FrameRate{24, 1});
    const FrameContext ctx = make_frame_context({
        .global_time = global,
        .local_time = local,
        .duration = Frame{30},
    });

    CHECK(ctx.global_time() == global);
    CHECK(ctx.local_time() == local);
    CHECK(ctx.frame() == Frame{10});
    CHECK(ctx.frame_rate() == FrameRate{24, 1});
}

TEST_CASE("FrameContext with_local_time") {
    const SampleTime global = SampleTime::from_frame_int(Frame{100}, FrameRate{30, 1});
    const FrameContext base = make_frame_context({
        .global_time = global,
        .duration = Frame{200},
        .width = 1280,
        .height = 720,
    });

    const SampleTime new_local = SampleTime::from_frame(12.5, FrameRate{30, 1});
    const FrameContext shifted = base.with_local_time(new_local, Frame{50});

    CHECK(shifted.global_time() == global);
    CHECK(shifted.local_time() == new_local);
    CHECK(shifted.frame() == Frame{12});
    CHECK(shifted.frame_fraction() == doctest::Approx(0.5));
    CHECK(shifted.duration() == Frame{50});
    CHECK(shifted.width == 1280);
    CHECK(shifted.height == 720);
}

TEST_CASE("FrameContext edge cases") {
    SUBCASE("First frame") {
        const FrameContext ctx = make_frame_context({
            .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
            .duration = Frame{100},
        });
        CHECK(ctx.is_first_frame());
        CHECK_FALSE(ctx.is_last_frame());
        CHECK(ctx.progress() == doctest::Approx(0.0));
    }

    SUBCASE("Last frame") {
        const FrameContext ctx = make_frame_context({
            .global_time = SampleTime::from_frame_int(Frame{99}, FrameRate{30, 1}),
            .duration = Frame{100},
        });
        CHECK_FALSE(ctx.is_first_frame());
        CHECK(ctx.is_last_frame());
        CHECK(ctx.progress() == doctest::Approx(0.99));
    }

    SUBCASE("Zero duration") {
        const FrameContext ctx = make_frame_context({
            .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
            .duration = Frame{0},
        });
        CHECK(ctx.progress() == doctest::Approx(0.0));
    }

    SUBCASE("Sub-frame fraction") {
        const FrameContext ctx = make_frame_context({
            .global_time = SampleTime::from_frame(10.25, FrameRate{24, 1}),
            .duration = Frame{100},
        });
        CHECK(ctx.frame() == Frame{10});
        CHECK(ctx.frame_fraction() == doctest::Approx(0.25));
    }
}

TEST_CASE("RationalTime preserves original presentation time base") {
    const RationalTime pts{90000, Rational{1, 90000}};
    CHECK(pts.seconds() == doctest::Approx(1.0));
    CHECK(rational_time_to_ticks_exact(pts, 960000) == 960000);

    const FrameTimeContext exact{
        .output_frame = Frame{30},
        .presentation_time = pts,
        .duration = RationalTime{3003, Rational{1, 90000}},
        .timeline_tick = 960000,
        .discontinuity = true,
    };
    const FrameContext ctx = make_frame_context({
        .global_time = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1}),
        .duration = Frame{100},
        .frame_time = exact,
    });

    CHECK(ctx.presentation_time() == pts);
    CHECK(ctx.presentation_duration() == exact.duration);
    CHECK(ctx.timeline_tick() == 960000);
    CHECK(ctx.discontinuity());
}

TEST_CASE("FrameRate exposes exact per-frame rational time") {
    const FrameRate ntsc{30000, 1001};
    CHECK(ntsc.frame_time_base() == Rational{1001, 30000});
    CHECK(ntsc.presentation_time(Frame{1001}) ==
          RationalTime{1001, Rational{1001, 30000}});
    CHECK(ntsc.frame_duration() == RationalTime{1, Rational{1001, 30000}});
}

TEST_CASE("Exact rational conversion rejects implicit rounding") {
    CHECK_THROWS_AS(
        rational_time_to_ticks_exact(RationalTime{1, Rational{1, 3}}, 10),
        std::invalid_argument);
}

TEST_CASE("TemporalRequirements are explicit and non-negative") {
    const TemporalRequirements frame_local{};
    CHECK(frame_local.valid());
    CHECK_FALSE(frame_local.is_temporal());

    const TemporalRequirements history{
        .history_frames = 1,
        .future_frames = 0,
        .history_duration = RationalTime{1, Rational{1, 30}},
    };
    CHECK(history.valid());
    CHECK(history.is_temporal());

    const TemporalRequirements invalid{.history_frames = -1};
    CHECK_FALSE(invalid.valid());
}
