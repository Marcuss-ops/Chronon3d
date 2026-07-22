#include <optional>
#include <doctest/doctest.h>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/font_engine.hpp>
using namespace chronon3d;


TEST_CASE("LayerBuilder: position_anim bakes into transform at frame") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(30, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080,
    .resource = std::pmr::get_default_resource()
});
    SceneBuilder sb(ctx);
    sb.layer("hero", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.position_anim()
            .key(0,  Vec3{0, 0, 0})
            .key(60, Vec3{120, 0, 0});
    });
    Scene scene = sb.build();
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(60.0f));
}

TEST_CASE("LayerBuilder: opacity_anim bakes into transform at frame") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(15, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080,
    .resource = std::pmr::get_default_resource()
});
    SceneBuilder sb(ctx);
    sb.layer("fade", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.opacity_anim()
            .key(0,  0.0f)
            .key(30, 1.0f);
    });
    Scene scene = sb.build();
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
}

TEST_CASE("LayerBuilder: static position() still works when no anim keys") {
    LayerBuilder b("layer", SampleTime{});
    b.rect("r", {.size = {10, 10}});
    b.position({100, 200, 0});
    Layer l = b.build();
    CHECK(l.transform.position.x == doctest::Approx(100.0f));
    CHECK(l.transform.position.y == doctest::Approx(200.0f));
}

TEST_CASE("LayerBuilder: scale_anim bakes into transform") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(30, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080,
    .resource = std::pmr::get_default_resource()
});
    SceneBuilder sb(ctx);
    sb.layer("s", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.scale_anim()
            .key(0,  Vec3{1, 1, 1})
            .key(60, Vec3{2, 2, 2});
    });
    Scene scene = sb.build();
    CHECK(scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
}

TEST_CASE("LayerBuilder: animations resolve in layer-local time") {
    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(90, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080,
    .resource = std::pmr::get_default_resource()
});
    SceneBuilder sb(ctx);
    sb.layer("offset", [](LayerBuilder& b) {
        b.from(30)
         .offset(10)
         .rect("r", {.size = {10, 10}});
        b.position_anim()
            .key(0,  Vec3{0, 0, 0})
            .key(60, Vec3{120, 0, 0});
    });
    Scene scene = sb.build();
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(120.0f));
    CHECK(scene.layers()[0].local_frame(90) == 70);
}

TEST_CASE("LayerBuilder: font_engine setter and getter") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    LayerBuilder b("layer", SampleTime{});
    CHECK(b.font_engine() == nullptr);

    b.font_engine(&engine);
    CHECK(b.font_engine() == &engine);
}
