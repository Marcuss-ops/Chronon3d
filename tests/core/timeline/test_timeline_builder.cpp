#include <doctest/doctest.h>
#include <chronon3d/timeline/timeline_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <memory_resource>
using namespace chronon3d;


TEST_CASE("TimelineBuilder: track timing filters layers by frame") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    SceneBuilder s(ctx);

    TimelineBuilder t(s);
    t.add("title").from(Frame{0}).duration(Frame{30}).with([](LayerBuilder& lb) {
        lb.rect("bg", {.size = {100, 100}, .color = Color::white()});
    })
    .add("subtitle").from(Frame{20}).duration(Frame{40}).with([](LayerBuilder& lb) {
        lb.rect("bg", {.size = {100, 100}, .color = Color::white()});
    })
    .apply();

    Scene scene = s.build();
    CHECK(scene.layers().size() == 1);
    CHECK(std::string(scene.layers()[0].name.c_str()) == "title");

    // At frame 25 both layers are active
    FrameContext ctx25 = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{25}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    SceneBuilder s25(ctx25);
    TimelineBuilder t25(s25);
    t25.add("title").from(Frame{0}).duration(Frame{30}).with([](LayerBuilder& lb) {
        lb.rect("bg", {.size = {100, 100}, .color = Color::white()});
    })
    .add("subtitle").from(Frame{20}).duration(Frame{40}).with([](LayerBuilder& lb) {
        lb.rect("bg", {.size = {100, 100}, .color = Color::white()});
    })
    .apply();

    Scene scene25 = s25.build();
    CHECK(scene25.layers().size() == 2);
}

TEST_CASE("TimelineBuilder: stagger shifts keyframes by spatial order") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    SceneBuilder s(ctx);

    TimelineBuilder t(s);
    t.add("a").from(Frame{0}).duration(Frame{60}).with([](LayerBuilder& lb) {
        lb.position({-100.0f, 0.0f, 0.0f});
        lb.slide_in({-200.0f, 0.0f, 0.0f}, Frame{20});
    })
    .add("b").from(Frame{0}).duration(Frame{60}).with([](LayerBuilder& lb) {
        lb.position({100.0f, 0.0f, 0.0f});
        lb.slide_in({0.0f, 0.0f, 0.0f}, Frame{20});
    })
    .stagger({"a", "b"}, StaggerConfig{.delay_per_unit = Frame{4}}, StaggerOrder::LeftToRight)
    .apply();

    Scene scene = s.build();
    REQUIRE(scene.layers().size() == 2);

    // Left-to-right: a (x=-100) comes first, then b (x=100).
    // a has rank 0 → delay 0 → keyframes stay at 0 and 20.
    // b has rank 1 → delay 4 → keyframes shift to 4 and 24.
    //
    // At global frame 0:
    //   a local frame 0 → evaluate(0) between key 0 (from -200) and 20 (to -100)
    //     → value is -200 (hold at start).
    //   b local frame 0 → evaluate(0) with shifted keys at 4 and 24.
    //     → 0 <= 4, hold at first key value = 0.
    const auto& layer_a = scene.layers()[0];
    const auto& layer_b = scene.layers()[1];

    CHECK(std::string(layer_a.name.c_str()) == "a");
    CHECK(std::string(layer_b.name.c_str()) == "b");

    // a is already sliding: at frame 0 it is at its slide_in 'from' position.
    CHECK(layer_a.transform.position.x == doctest::Approx(-200.0f));
    // b is delayed by 4 frames: at frame 0 its first shifted keyframe is at frame 4,
    // so evaluate(0) returns the first keyframe value (0,0,0).
    CHECK(layer_b.transform.position.x == doctest::Approx(0.0f));
}

TEST_CASE("TimelineBuilder: camera track sets animated camera") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    SceneBuilder s(ctx);

    AnimatedCamera2_5D cam;
    cam.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f});
    cam.position.key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f});

    TimelineBuilder t(s);
    t.add("push_in").from(Frame{0}).duration(Frame{60}).with_camera(cam)
    .apply();

    Scene scene = s.build();
    auto& static_cam = scene.camera_2_5d();
    CHECK(static_cam.position.z == doctest::Approx(-750.0f)); // half-way between -1000 and -500 at frame 30
}

TEST_CASE("TimelineBuilder: global stagger applies to all tracks") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    SceneBuilder s(ctx);

    TimelineBuilder t(s);
    t.add("x").from(Frame{0}).duration(Frame{60}).with([](LayerBuilder& lb) {
        lb.position({0.0f, 0.0f, 0.0f});
        lb.slide_in({-50.0f, 0.0f, 0.0f}, Frame{10});
    })
    .add("y").from(Frame{0}).duration(Frame{60}).with([](LayerBuilder& lb) {
        lb.position({200.0f, 0.0f, 0.0f});
        lb.slide_in({150.0f, 0.0f, 0.0f}, Frame{10});
    })
    .stagger(StaggerConfig{.delay_per_unit = Frame{6}}, StaggerOrder::LeftToRight)
    .apply();

    Scene scene = s.build();
    REQUIRE(scene.layers().size() == 2);

    // x is Left (x=0), y is Right (x=200).
    // x delay 0 → keyframes at 0 and 10. At frame 0 → -50.
    // y delay 6 → keyframes at 6 and 16. At frame 0 → 150 (hold at first key).
    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(-50.0f));
    CHECK(scene.layers()[1].transform.position.x == doctest::Approx(150.0f));
}
