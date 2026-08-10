#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/scene/model/layer/layer_time_resolver.hpp>

using namespace chronon3d;

TEST_CASE("Temporal contract resolves half-open frame ranges") {
    const FrameRate fps30{30, 1};

    const auto range = resolve_frame_range(0.0, 3.0, fps30);
    CHECK(range.start == Frame{0});
    CHECK(range.end == Frame{90});
    CHECK(range.contains(Frame{0}));
    CHECK(range.contains(Frame{89}));
    CHECK_FALSE(range.contains(Frame{90}));

    const auto half_second = resolve_frame_range(0.5, 1.0, fps30);
    CHECK(half_second.start == Frame{15});
    CHECK(half_second.end == Frame{30});
}

TEST_CASE("Temporal contract applies the minimum one-frame policy once") {
    const FrameRate fps30{30, 1};

    const auto empty = resolve_frame_range(
        1.0, 1.0, fps30, MinimumFrameDuration::AllowEmpty);
    CHECK(empty.duration() == Frame{0});
    CHECK_FALSE(empty.contains(Frame{30}));

    const auto one_frame = resolve_frame_range(
        1.0, 1.0, fps30, MinimumFrameDuration::AtLeastOneFrame);
    CHECK(one_frame.start == Frame{30});
    CHECK(one_frame.end == Frame{31});
    CHECK(one_frame.contains(Frame{30}));
    CHECK_FALSE(one_frame.contains(Frame{31}));
}

TEST_CASE("Temporal contract is deterministic for fractional frame rates") {
    const FrameRate ntsc{30000, 1001};
    const auto range = resolve_frame_range(0.5, 1.0, ntsc);
    CHECK(range.start == Frame{15});
    CHECK(range.end == Frame{30});

    const SampleTime time = SampleTime::from_frame_int(Frame{60}, FrameRate{60, 1});
    CHECK(time.seconds() == doctest::Approx(1.0));
    CHECK(resolve_frame_range(1.0, 2.0, time.frame_rate).start == Frame{60});
}

TEST_CASE("Layer activity uses the same half-open boundary contract") {
    CHECK(LayerTimeResolver::active_at(Frame{0}, Frame{0}, Frame{90}));
    CHECK(LayerTimeResolver::active_at(Frame{89}, Frame{0}, Frame{90}));
    CHECK_FALSE(LayerTimeResolver::active_at(Frame{90}, Frame{0}, Frame{90}));
    CHECK(LayerTimeResolver::active_at(Frame{90}, Frame{90}, Frame{30}));
    CHECK(LayerTimeResolver::active_at(Frame{100}, Frame{90}, Frame{-1}));
}
