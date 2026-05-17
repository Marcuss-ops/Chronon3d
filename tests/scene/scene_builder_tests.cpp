#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

TEST_CASE("Test 8.1 — Rect mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .rect("r", {.size = {120, 80}, .color = Color::red(), .pos = {10, 20, 0}})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.name == "r");
    CHECK(node.shape.type == ShapeType::Rect);
    CHECK(node.shape.rect.size.x == 120.0f);
    CHECK(node.shape.rect.size.y == 80.0f);
    CHECK(node.color == Color::red());
    CHECK(node.world_transform.position.x == 10.0f);
}

TEST_CASE("Test 8.2 — Circle mapping on SceneBuilder") {
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

TEST_CASE("Test 8.3 — Rounded rect mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .rounded_rect("rr", {.size = {100, 100}, .radius = 12.0f, .color = Color::blue()})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::RoundedRect);
    CHECK(node.shape.rounded_rect.radius == 12.0f);
}

TEST_CASE("Test 8.4 — Line mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .line("l", {.from = {0, 0, 0}, .to = {100, 100, 0}, .thickness = 3.0f, .color = Color::white()})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::Line);
    CHECK(node.shape.line.to.x == 100.0f);
    CHECK(node.shape.line.thickness == 3.0f);
}

TEST_CASE("Test 8.5 — Text mapping on SceneBuilder") {
    TextStyle style;
    style.font_path = "Inter-Regular.ttf";
    style.size = 24.0f;
    style.color = Color::black();

    auto scene = SceneBuilder{}
        .text("t", {.content = "test text", .style = style})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::Text);
    CHECK(node.shape.text.text == "test text");
    CHECK(node.shape.text.style.size == 24.0f);
}

TEST_CASE("Test 8.6 — Image mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .image("img", {.path = "photo.png"})
        .build();

    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.shape.type == ShapeType::Image);
    CHECK(node.shape.image.path == "photo.png");
}

TEST_CASE("Test 8.7 — Active/inactive layer lifecycle filters") {
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

TEST_CASE("Test 8.8 — Layer adjustment mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .adjustment_layer("adj", [](LayerBuilder& l) {
            l.blur(10.0f);
        })
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "adj");
    CHECK(scene.layers()[0].kind == LayerKind::Adjustment);
}

TEST_CASE("Test 8.9 — Layer precomp mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .precomp_layer("nested", "nested_comp_id", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    CHECK(layer.kind == LayerKind::Precomp);
    CHECK(layer.precomp_composition_name == "nested_comp_id");
}

TEST_CASE("Test 8.10 — Layer video mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .video_layer("video", "movie.mp4", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    CHECK(layer.kind == LayerKind::Video);
    REQUIRE(layer.video_source != nullptr);
    CHECK(layer.video_source->path == "movie.mp4");
}

TEST_CASE("Test 8.11 — Layer null mapping on SceneBuilder") {
    auto scene = SceneBuilder{}
        .null_layer("null_node", [](LayerBuilder& l) {})
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Null);
}

TEST_CASE("Test 8.12 — resolve_hierarchy is called at build()") {
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
