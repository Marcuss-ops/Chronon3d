#include <doctest/doctest.h>

#include <chronon3d/core/types/frame_context.hpp>

using namespace chronon3d;

namespace {

FrameContext make_local_context(Frame global, Frame local, Frame duration) {
    const FrameRate rate{30, 1};
    return make_frame_context({
        .global_time = SampleTime::from_frame_int(global, rate),
        .local_time = SampleTime::from_frame_int(local, rate),
        .duration = duration,
    });
}

} // namespace

TEST_CASE("FrameContext — canonical local frame and progress") {
    const auto ctx = make_local_context(Frame{75}, Frame{15}, Frame{30});

    CHECK(ctx.global_time().integral_frame() == Frame{75});
    CHECK(ctx.local_time().integral_frame() == Frame{15});
    CHECK(ctx.frame() == Frame{15});
    CHECK(ctx.progress() == doctest::Approx(0.5));
    CHECK(ctx.seconds() == doctest::Approx(0.5));
}

TEST_CASE("FrameContext — local time is independent of global time") {
    const auto before = make_local_context(Frame{50}, Frame{0}, Frame{30});
    const auto after = make_local_context(Frame{90}, Frame{0}, Frame{30});

    CHECK(before.frame() == Frame{0});
    CHECK(after.frame() == Frame{0});
    CHECK(before.progress() == doctest::Approx(0.0));
    CHECK(after.progress() == doctest::Approx(0.0));
}

TEST_CASE("FrameContext — zero duration progress guard") {
    const auto ctx = make_local_context(Frame{10}, Frame{0}, Frame{0});

    CHECK(ctx.progress() == doctest::Approx(0.0));
    CHECK(ctx.duration() == Frame{0});
}
