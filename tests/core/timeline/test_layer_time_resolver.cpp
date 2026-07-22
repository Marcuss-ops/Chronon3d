#include <doctest/doctest.h>
#include <chronon3d/scene/model/layer/layer_time_resolver.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

using namespace chronon3d;

namespace {

// Convenience: build a Layer from a LayerBuilder so we can test the public
// Layer::local_time() path as well as the direct resolver path.
Layer make_layer_with_duration(Frame from, Frame duration) {
    LayerBuilder lb("test", SampleTime::from_frame_int(0, FrameRate{30, 1}));
    lb.from(from).duration(duration);
    return lb.build();
}

} // namespace

TEST_CASE("LayerTimeResolver: identity with offset") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_offset = Frame{10};

    auto t = SampleTime::from_frame(25.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(35.0));
    CHECK(local.frame_rate == rate);
}

TEST_CASE("LayerTimeResolver: freeze frame") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_remap.freeze_frame = Frame{7};

    auto t = SampleTime::from_frame(25.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.integral_frame() == Frame{7});
}

TEST_CASE("LayerTimeResolver: speed 0.5") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_remap.speed = 0.5f;

    auto t = SampleTime::from_frame(40.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(20.0));
}

TEST_CASE("LayerTimeResolver: speed 2.0") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_remap.speed = 2.0f;

    auto t = SampleTime::from_frame(15.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(30.0));
}

TEST_CASE("LayerTimeResolver: reverse") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_remap.speed = -1.0f;

    auto t = SampleTime::from_frame(10.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(90.0));
}

TEST_CASE("LayerTimeResolver: animated time remap") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
    layer.time_remap.time_remap.key(0, 0.0f).key(30, 60.0f);

    auto t = SampleTime::from_frame(15.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(30.0).epsilon(0.01));
}

TEST_CASE("LayerTimeResolver: negative frames") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{-50}, Frame{100});
    layer.time_offset = Frame{5};

    auto t = SampleTime::from_frame(-20.0, rate);
    auto local = layer.local_time(t);
    CHECK(local.frame == doctest::Approx(-20.0 - (-50) + 5.0));
}

TEST_CASE("LayerTimeResolver: random access") {
    const FrameRate rate{30, 1};
    Layer layer = make_layer_with_duration(Frame{0}, Frame{100});

    CHECK(layer.local_time(SampleTime::from_frame(10.0, rate)).frame == doctest::Approx(10.0));
    CHECK(layer.local_time(SampleTime::from_frame(42.0, rate)).frame == doctest::Approx(42.0));
    CHECK(layer.local_time(SampleTime::from_frame(87.5, rate)).frame == doctest::Approx(87.5));
}

TEST_CASE("LayerTimeResolver: local_time agrees with deprecated local_frame") {
    const FrameRate rate{30, 1};

    auto run_equivalence = [&](f32 speed, Frame offset) {
        Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
        layer.time_offset = offset;
        layer.time_remap.speed = speed;

        for (Frame f = -10; f <= 40; f += 5) {
            auto st = SampleTime::from_frame_int(f, rate);
            auto via_local_time = layer.local_time(st).integral_frame();
            auto via_local_frame = layer.local_frame(f, rate);
            CHECK(via_local_time == via_local_frame);
        }
    };

    run_equivalence(1.0f, Frame{0});
    run_equivalence(0.5f, Frame{5});
    run_equivalence(2.0f, Frame{-3});
    run_equivalence(-1.0f, Frame{0});
}

TEST_CASE("LayerTimeResolver: sub-frame positions are rate-agnostic") {
    const FrameRate rates[] = {
        FrameRate{24, 1},
        FrameRate{25, 1},
        FrameRate{30, 1},
        FrameRate{60, 1},
        FrameRate{30000, 1001},
    };

    for (const auto& rate : rates) {
        Layer layer = make_layer_with_duration(Frame{0}, Frame{100});

        auto t10    = SampleTime::from_frame(10.0, rate);
        auto t10_25 = SampleTime::from_frame(10.25, rate);
        auto t10_5  = SampleTime::from_frame(10.5, rate);

        CHECK(layer.local_time(t10).frame == doctest::Approx(10.0));
        CHECK(layer.local_time(t10_25).frame == doctest::Approx(10.25));
        CHECK(layer.local_time(t10_5).frame == doctest::Approx(10.5));
    }
}

TEST_CASE("LayerTimeResolver: rate matrix 24/25/30/60/NTSC with speed 2.0") {
    const FrameRate rates[] = {
        FrameRate{24, 1},
        FrameRate{25, 1},
        FrameRate{30, 1},
        FrameRate{60, 1},
        FrameRate{30000, 1001},
    };

    for (const auto& rate : rates) {
        Layer layer = make_layer_with_duration(Frame{0}, Frame{100});
        layer.time_remap.speed = 2.0f;

        auto t = SampleTime::from_frame(12.5, rate);
        auto local = layer.local_time(t);
        CHECK(local.frame == doctest::Approx(25.0));
        CHECK(local.frame_rate == rate);
    }
}
