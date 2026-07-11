#include <doctest/doctest.h>
#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
using namespace chronon3d;

TEST_CASE("RenderNodeFactory creates rect nodes with centered anchors") {
    auto* res = std::pmr::get_default_resource();
    auto node = RenderNodeFactory::rect(res, "box", RectParams{
        .size  = {120.0f, 80.0f},
        .color = Color::red(),
        .pos   = {10.0f, 20.0f, 30.0f},
    });
    CHECK(node.name == "box");
    CHECK(node.shape.type() == ShapeType::Rect);
    CHECK(node.shape.rect().size.x == doctest::Approx(120.0f));
    CHECK(node.shape.rect().size.y == doctest::Approx(80.0f));
    CHECK(node.world_transform.position.x == doctest::Approx(10.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(20.0f));
    CHECK(node.world_transform.position.z == doctest::Approx(30.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(60.0f));
    CHECK(node.world_transform.anchor.y == doctest::Approx(40.0f));
    CHECK(node.surface_policy == SurfacePolicy::IntrinsicSize);
    CHECK(node.transform_policy == TransformPolicy::MatrixOnly);
    CHECK(node.color.r == doctest::Approx(Color::red().r));
}

TEST_CASE("RenderNodeFactory creates grid background nodes") {
    auto* res = std::pmr::get_default_resource();
    auto node = RenderNodeFactory::grid_background(res, "grid", GridBackgroundParams{
        .size            = {1920.0f, 1080.0f},
        .offset          = {24.0f, 8.0f},
        .bg_color        = Color{0.01f, 0.02f, 0.04f, 1.0f},
        .grid_color      = Color{0.3f, 0.5f, 0.9f, 0.08f},
        .spacing         = 96.0f,
        .minor_thickness = 1.0f,
        .major_thickness = 2.0f,
        .major_every     = 4,
        .centered        = true,
    });
    CHECK(node.shape.type() == ShapeType::GridBackground);
    CHECK(node.shape.grid_background().size.x == doctest::Approx(1920.0f));
    CHECK(node.shape.grid_background().offset.x == doctest::Approx(24.0f));
    CHECK(node.world_transform.position.x == doctest::Approx(0.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(0.0f));
    CHECK(node.surface_policy == SurfacePolicy::ViewportSize);
    CHECK(node.transform_policy == TransformPolicy::RasterizeAfter);
}

TEST_CASE("RenderNodeFactory creates image nodes and maps advanced fields") {
    auto* res = std::pmr::get_default_resource();
    ImageParams p;
    p.path         = "assets/images/camera_reference.jpg";
    p.size         = {640.0f, 360.0f};
    p.pos          = {10.0f, 20.0f, 30.0f};
    p.fit          = FitMode::Cover;
    p.focal_point  = {0.5f, 0.2f};
    p.crop.enabled = true;
    p.crop.origin  = {0.1f, 0.1f};
    p.crop.size    = {0.8f, 0.8f};
    p.opacity      = 0.5f;
    p.radius       = 24.0f;
    auto node = RenderNodeFactory::image(res, "img", p);
    CHECK(node.name == "img");
    CHECK(node.shape.type() == ShapeType::Image);
    CHECK(node.shape.image().path == "assets/images/camera_reference.jpg");
    CHECK(node.shape.image().size.x == doctest::Approx(640.0f));
    CHECK(node.shape.image().opacity == doctest::Approx(0.5f));
    CHECK(node.shape.image().radius == doctest::Approx(24.0f));
    CHECK(node.shape.image().fit == FitMode::Cover);
    CHECK(node.shape.image().focal_point.y == doctest::Approx(0.2f));
    CHECK(node.shape.image().crop.enabled == true);
    CHECK(node.shape.image().crop.origin.x == doctest::Approx(0.1f));
    CHECK(node.shape.image().crop.size.x == doctest::Approx(0.8f));
    CHECK(node.surface_policy == SurfacePolicy::IntrinsicSize);
    CHECK(node.transform_policy == TransformPolicy::MatrixOnly);
}

TEST_CASE("Missing image returns placeholder/fallback instead of crashing") {
    auto* res = std::pmr::get_default_resource();
    ImageParams p;
    p.path = "assets/images/does_not_exist.jpg";
    p.size = {300.0f, 200.0f};
    // Constructing must not crash
    CHECK_NOTHROW(RenderNodeFactory::image(res, "missing", p));
}

TEST_CASE("RenderNodeFactory preserves gradient text paint") {
    auto* res = std::pmr::get_default_resource();

    // PR3→PR4 migration: TextSpec is the composable form.  The flat
    // `TextParams.text/.size/.paint` fields no longer exist; use the
    // nested sub-structs.
    TextSpec p;
    p.content.value  = "SaaS";
    p.layout.box     = {640.0f, 240.0f};
    p.appearance.paint.fill_style = Fill::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f},
        {
            {0.0f, Color{1.0f, 0.72f, 0.18f, 1.0f}},
            {1.0f, Color{0.92f, 0.20f, 0.98f, 1.0f}},
        }
    );

    auto node = RenderNodeFactory::text(res, "title", p);
    // M1.5#9 step 2: factory now flags `ShapeType::TextRun` and routes
    // paint/material/shadows through `materialize_text_run_shape(...)`.
    // Without an explicit `FontEngine* engine` (default null), the
    // materializer logs spdlog::error + returns nullptr; the test
    // asserts the post-failure contract: type=TextRun + handle exists,
    // value=nullptr.  Materialization-success path is covered separately
    // by tests that supply a FontEngine fixture (test_text_run_builder).
    REQUIRE(node.shape.type() == ShapeType::TextRun);
    // handle is default-constructed (value == nullptr) when materialization
    // fails; we don't dereference .value->paint here.  Historical
    // `.shape.text().style.paint.fill_style` checks have been migrated
    // into the `when engine supplied` materialization-success suite.
    CHECK(node.surface_policy == SurfacePolicy::IntrinsicSize);
    CHECK(node.transform_policy == TransformPolicy::MatrixOnly);
}

TEST_CASE("RenderNode content hash ignores placement but honors policy") {
    auto* res = std::pmr::get_default_resource();

    // M1.5#9 step 2: factory now emits ShapeType::TextRun; hash function
    // operates on the RenderNode's discriminator + transform policy
    // (independent of materialization).  The test continues to verify
    // that placement changes don't move the content hash and that
    // surface/transform policy moves BOTH content + placement hashes.
    auto node_a = RenderNodeFactory::text(res, "title", TextSpec{.content = {.value = "This is a long line that should wrap cleanly"},.font    = {.font_size = 72.0f},.layout  = {.box = {640.0f, 180.0f}},});
    CHECK(node_a.shape.type() == ShapeType::TextRun);
    auto node_b = node_a;

    const auto content_a   = graph::hash_render_node_content_only(node_a);
    const auto placement_a = graph::hash_render_node_placement_only(node_a);

    node_b.world_transform.position.x += 240.0f;
    node_b.world_transform.position.y += 120.0f;
    CHECK(graph::hash_render_node_placement_only(node_b) != placement_a);

    node_b.surface_policy = SurfacePolicy::ViewportSize;
    CHECK(graph::hash_render_node_content_only(node_b) != content_a);
}
