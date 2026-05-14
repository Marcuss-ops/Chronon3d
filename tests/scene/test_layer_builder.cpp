#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

TEST_CASE("SceneBuilder creates layer") {
    SceneBuilder s;

    s.layer("group", [](LayerBuilder& l) {
        l.position({100, 200, 0});
        l.rect("box", {
            .size = {50, 50},
            .color = Color{1, 0, 0, 1},
            .pos = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "group");
    CHECK(scene.layers()[0].nodes.size() == 1);
    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(100.0f));
}

TEST_CASE("Layer from/duration controls visibility") {
    FrameContext ctx_early{ .frame = 5, .resource = std::pmr::get_default_resource() };
    FrameContext ctx_active{ .frame = 15, .resource = std::pmr::get_default_resource() };
    FrameContext ctx_late{ .frame = 25, .resource = std::pmr::get_default_resource() };

    auto build_with = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("test", [](LayerBuilder& l) {
            l.from(10).duration(10);
            l.rect("box", {});
        });
        return s.build();
    };

    CHECK(build_with(ctx_early).layers().empty());
    CHECK(build_with(ctx_active).layers().size() == 1);
    CHECK(build_with(ctx_late).layers().empty());
}

TEST_CASE("LayerBuilder resolves depth role only once") {
    LayerBuilder b("test");
    auto layer = b.enable_3d()
                  .depth_role(DepthRole::Foreground)
                  .depth_offset(25.0f)
                  .build();

    // Foreground is -500.0f usually. -500 + 25 = -475.
    CHECK(layer.transform.position.z == doctest::Approx(-475.0f));
}
