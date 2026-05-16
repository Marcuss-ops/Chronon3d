#include <doctest/doctest.h>
#include <chronon3d/timeline/sequence.hpp>

using namespace chronon3d;

TEST_CASE("Sequence — active window") {
    FrameContext ctx;
    ctx.frame = Frame{50};
    ctx.frame_rate = {30, 1};

    SUBCASE("before sequence: inactive, frame=0, progress=0") {
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == false);
        CHECK(seq.frame == 0);
        CHECK(seq.progress() == 0.0f);
    }

    SUBCASE("at start frame: active, local frame=0, progress=0") {
        ctx.frame = Frame{60};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == true);
        CHECK(seq.frame == 0);
        CHECK(seq.progress() == 0.0f);
    }

    SUBCASE("mid-sequence: correct local frame and progress") {
        ctx.frame = Frame{75};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == true);
        CHECK(seq.frame == 15);
        CHECK(seq.progress() == doctest::Approx(0.5f));
        CHECK(seq.seconds() == doctest::Approx(0.5f));
    }

    SUBCASE("at end frame (exclusive): inactive") {
        ctx.frame = Frame{90};
        auto seq = sequence(ctx, Frame{60}, Frame{30});
        CHECK(seq.active == false);
    }

    SUBCASE("sequence starting at frame 0") {
        ctx.frame = Frame{0};
        auto seq = sequence(ctx, Frame{0}, Frame{30});
        CHECK(seq.active == true);
        CHECK(seq.frame == 0);
    }
}

TEST_CASE("Sequence — held_progress") {
    FrameContext ctx;
    ctx.frame_rate = {30, 1};

    SUBCASE("before sequence: held_progress returns 0") {
        ctx.frame = Frame{5};
        auto seq = sequence(ctx, Frame{20}, Frame{25});
        CHECK(seq.held_progress() == 0.0f);
    }

    SUBCASE("at start: held_progress == progress == 0") {
        ctx.frame = Frame{20};
        auto seq = sequence(ctx, Frame{20}, Frame{25});
        CHECK(seq.held_progress() == doctest::Approx(seq.progress()));
    }

    SUBCASE("mid-sequence: held_progress == progress") {
        ctx.frame = Frame{30};
        auto seq = sequence(ctx, Frame{20}, Frame{25});
        CHECK(seq.active == true);
        CHECK(seq.held_progress() == doctest::Approx(seq.progress()));
    }

    SUBCASE("after sequence: held_progress returns 1") {
        ctx.frame = Frame{50};
        auto seq = sequence(ctx, Frame{20}, Frame{25});
        CHECK(seq.active == false);
        CHECK(seq.held_progress() == doctest::Approx(1.0f));
    }

    SUBCASE("immediately after end: held_progress returns 1") {
        ctx.frame = Frame{45};  // from=20, duration=25, end=45 (exclusive)
        auto seq = sequence(ctx, Frame{20}, Frame{25});
        CHECK(seq.active == false);
        CHECK(seq.held_progress() == doctest::Approx(1.0f));
    }
}

TEST_CASE("Sequence — zero duration guard") {
    FrameContext ctx;
    ctx.frame = Frame{10};
    ctx.frame_rate = {30, 1};

    auto seq = sequence(ctx, Frame{10}, Frame{0});
    CHECK(seq.progress() == 0.0f);
    CHECK(seq.held_progress() == doctest::Approx(1.0f));
}
