#include <doctest/doctest.h>
#include <chronon3d/core/frame_context.hpp>

using namespace chronon3d;

TEST_CASE("FrameContext basics") {
    FrameContext ctx{
        .frame = 30,
        .duration = 100,
        .frame_rate = {30, 1}
    };

    SUBCASE("Time conversion") {
        CHECK(ctx.seconds() == doctest::Approx(1.0f));
    }

    SUBCASE("Progress calculation") {
        CHECK(ctx.progress() == doctest::Approx(0.3f));
    }

    SUBCASE("Boundary checks") {
        CHECK_FALSE(ctx.is_first_frame());
        CHECK_FALSE(ctx.is_last_frame());
    }
}

TEST_CASE("FrameContext edge cases") {
    SUBCASE("First frame") {
        FrameContext ctx{.frame = 0, .duration = 100};
        CHECK(ctx.is_first_frame());
        CHECK_FALSE(ctx.is_last_frame());
        CHECK(ctx.progress() == 0.0f);
    }

    SUBCASE("Last frame") {
        FrameContext ctx{.frame = 99, .duration = 100};
        CHECK_FALSE(ctx.is_first_frame());
        CHECK(ctx.is_last_frame());
        CHECK(ctx.progress() == doctest::Approx(0.99f));
    }

    SUBCASE("Zero duration") {
        FrameContext ctx{.frame = 0, .duration = 0};
        CHECK(ctx.progress() == 0.0f); // No crash
    }
}
