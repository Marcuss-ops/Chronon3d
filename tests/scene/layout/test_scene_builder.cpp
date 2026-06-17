#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/registry/shape_ids.hpp>
using namespace chronon3d;

namespace shape_ids = chronon3d::registry::shape_ids;

TEST_CASE("SceneBuilder minimal") {
    SceneBuilder builder;
    builder.rect("test-box", {.size={100, 100}, .color=Color::red(), .pos={10, 20, 30}});
    
    Scene scene = builder.build();
    
    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.name == "test-box");
    CHECK(node.world_transform.position.x == 10.0f);
    CHECK(node.color.r == Color::red().r);
    CHECK(node.color.g == Color::red().g);
    CHECK(node.color.b == Color::red().b);
    CHECK(node.color.a == Color::red().a);
}

TEST_CASE("SceneBuilder path API") {
    auto scene = SceneBuilder{}
        .path("my-path", {
            .commands = {
                PathCommand::move_to({0, 0}),
                PathCommand::line_to({100, 100})
            },
            .stroke = {.width = 5.0f},
            .pos = {10, 20, 0}
        })
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.name == "my-path");
    CHECK(node.shape.type == ShapeType::Path);
    CHECK(node.shape.path.commands.size() == 2);
    CHECK(node.shape.path.stroke.width == doctest::Approx(5.0f));
    CHECK(node.world_transform.position.x == doctest::Approx(10.0f));
}

TEST_CASE("SceneBuilder fluent API") {
    auto scene = SceneBuilder{}
        .rect("box1", {.size={100, 100}, .color=Color::white(), .pos={0, 0, 0}})
        .rect("box2", {.size={100, 100}, .color=Color::black(), .pos={1, 1, 1}})
        .build();
        
    CHECK(scene.nodes().size() == 2);
}

TEST_CASE("SceneBuilder lighting API stores scene lights") {
    auto scene = SceneBuilder{}
        .ambient_light(Color{0.8f, 0.7f, 0.6f, 1.0f}, 0.25f)
        .directional_light({0.0f, 0.0f, -1.0f}, Color{1.0f, 0.9f, 0.8f, 1.0f}, 0.75f)
        .build();

    CHECK(scene.light_context().enabled);
    CHECK(scene.light_context().ambient_enabled);
    CHECK(scene.light_context().directional_enabled);
    CHECK(scene.light_context().ambient == doctest::Approx(0.25f));
    CHECK(scene.light_context().diffuse == doctest::Approx(0.75f));
    CHECK(scene.light_context().ambient_color.r == doctest::Approx(0.8f));
    CHECK(scene.light_context().directional_color.g == doctest::Approx(0.9f));
}

TEST_CASE("LayerBuilder accepts_lights toggles material") {
    auto scene = SceneBuilder{}
        .layer("card", [](LayerBuilder& l) {
            Material2_5D mat;
            mat.accepts_lights = false;
            mat.ambient_multiplier = 0.5f;
            l.material(mat);
            l.rect("fill", {.size = {100, 100}, .color = Color::white(), .pos = {0, 0, 0}});
        })
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].material.accepts_lights == false);
    CHECK(scene.layers()[0].material.ambient_multiplier == doctest::Approx(0.5f));
}

TEST_CASE("SceneBuilder generic shape API creates rect") {
    auto scene = SceneBuilder{}
        .shape(registry::shape_ids::Rect, "box", RectParams{
            .size = {100, 50},
            .color = Color::white(),
            .pos = {1, 2, 3}
        })
        .build();

    REQUIRE(scene.nodes().size() == 1);
    CHECK(scene.nodes()[0].name == "box");
    CHECK(scene.nodes()[0].shape.type == ShapeType::Rect);
    CHECK(scene.nodes()[0].world_transform.position.z == doctest::Approx(3.0f));
}

TEST_CASE("SceneBuilder circle primitive") {
    auto scene = SceneBuilder{}
        .circle("c", {.radius = 45.0f, .color = Color::green(), .pos = {5, 5, 0}})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.name == "c");
    CHECK(node.shape.type == ShapeType::Circle);
    CHECK(node.shape.circle.radius == 45.0f);
    CHECK(node.color == Color::green());
}

TEST_CASE("SceneBuilder rounded rect primitive") {
    auto scene = SceneBuilder{}
        .rounded_rect("rr", {.size = {100, 100}, .radius = 12.0f, .color = Color::blue()})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::RoundedRect);
    CHECK(node.shape.rounded_rect.radius == 12.0f);
}

TEST_CASE("SceneBuilder line primitive") {
    auto scene = SceneBuilder{}
        .line("l", {.from = {0, 0, 0}, .to = {100, 100, 0}, .thickness = 3.0f, .color = Color::white()})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::Line);
    CHECK(node.shape.line.to.x == 100.0f);
    CHECK(node.shape.line.thickness == 3.0f);
}

TEST_CASE("SceneBuilder image primitive") {
    auto scene = SceneBuilder{}
        .image("img", {.path = "photo.png"})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::Image);
    CHECK(node.shape.image.path == "photo.png");
}

TEST_CASE("SceneBuilder active/inactive layer lifecycle filters") {
    FrameContext ctx{.frame = 10};
    auto scene = SceneBuilder{ctx}
        .layer("active-layer", [](LayerBuilder& l) {
            l.from(0).duration(20);
        })
        .layer("inactive-layer", [](LayerBuilder& l) {
            l.from(50).duration(10);
        })
        .build();

    // The inactive layer is filtered out at frame 10 because it starts at 50
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "active-layer");
}

TEST_CASE("SceneBuilder layer adjustment mapping") {
    auto scene = SceneBuilder{}
        .adjustment_layer("adj", [](LayerBuilder& l) {
            l.blur(10.0f);
        })
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "adj");
    CHECK(scene.layers()[0].kind == LayerKind::Adjustment);
}

TEST_CASE("SceneBuilder layer precomp mapping") {
    auto scene = SceneBuilder{}
        .precomp_layer("nested", "nested_comp_id", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    CHECK(layer.kind == LayerKind::Precomp);
    CHECK(layer.precomp_composition_name == "nested_comp_id");
}

TEST_CASE("SceneBuilder layer video mapping") {
    auto scene = SceneBuilder{}
        .video_layer("video", "movie.mp4", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    CHECK(layer.kind == LayerKind::Video);
    REQUIRE(layer.video_source != nullptr);
    CHECK(layer.video_source->path == "movie.mp4");
}

TEST_CASE("SceneBuilder layer null mapping") {
    auto scene = SceneBuilder{}
        .null_layer("null_node", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Null);
}

TEST_CASE("SceneBuilder resolve_hierarchy is called at build()") {
    auto scene = SceneBuilder{}
        .null_layer("parent", [](LayerBuilder& l) {
            l.position({100, 0, 0});
        })
        .layer("child", [](LayerBuilder& l) {
            l.parent("parent").position({20, 0, 0});
        })
        .build();

    // Scene build() automatically resolves the hierarchy
    REQUIRE(scene.layers().size() == 2);
    // After resolution, the child's position (world coordinate) should be 120
    CHECK(scene.layers()[1].transform.position.x == doctest::Approx(120.0f));
}
