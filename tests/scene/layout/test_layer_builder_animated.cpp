#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/frame_context.hpp>

using namespace chronon3d;

TEST_CASE("LayerBuilder: position_anim bakes into transform at frame") {
    FrameContext ctx{.frame = 30, .resource = std::pmr::get_default_resource()};
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
    FrameContext ctx{.frame = 15, .resource = std::pmr::get_default_resource()};
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
    LayerBuilder b("layer");
    b.rect("r", {.size = {10, 10}});
    b.position({100, 200, 0});
    Layer l = b.build();
    CHECK(l.transform.position.x == doctest::Approx(100.0f));
    CHECK(l.transform.position.y == doctest::Approx(200.0f));
}

TEST_CASE("LayerBuilder: scale_anim bakes into transform") {
    FrameContext ctx{.frame = 30, .resource = std::pmr::get_default_resource()};
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
    FrameContext ctx{.frame = 90, .resource = std::pmr::get_default_resource()};
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
