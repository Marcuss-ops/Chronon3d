// test_text_placement_resolver_coordinate_contract.cpp — Split from
// test_text_placement_resolver.cpp: rasterizer-independent Canvas and
// layer-pin coordinate locks (CoordinateContract), including the
// LayerBuilder materialization contract.

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement.hpp>           // TextPlacement, TextPlacementKind
#include <chronon3d/text/resolve_text_placement.hpp>    // Canonical resolver surface
#include <chronon3d/layout/layout_solver.hpp>            // Canvas-space layer pin math
#include <chronon3d/scene/builders/layer_builder.hpp>    // Materialized TextRun transform contract
#include <chronon3d/scene/model/core/scene.hpp>         // Rasterizer-independent layer contract

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

static constexpr f32 kEps = 0.01f;

static bool vec2_near(Vec2 a, Vec2 b, f32 eps = kEps) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}

static bool mat4_near(const Mat4& a, const Mat4& b, f32 eps = kEps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a[i][j] - b[i][j]) > eps) return false;
    return true;
}

static CanvasInfo default_canvas() {
    return CanvasInfo{1920.0f, 1080.0f, 54.0f, 54.0f, 96.0f, 96.0f};
}

// ============================================================================
// CoordinateContract — rasterizer-independent Canvas and layer-pin locks
// ============================================================================

TEST_CASE("CoordinateContract: Absolute pin remains in Canvas coordinates") {
    const CanvasInfo canvas = default_canvas();
    const Vec2 box{400.0f, 100.0f};
    const TextPlacement placement{
        TextPlacementKind::Absolute, {200.0f, 100.0f}};

    const Vec2 pin = resolve_placement_origin(canvas, box, placement);
    const auto resolved = resolve_text_placement(
        canvas, box, placement, TextAnchor::TopLeft);

    // Absolute(200, 100) is a Canvas pin. It is not converted to the
    // render graph's centered basis by the canonical resolver.
    CHECK(vec2_near(pin, {200.0f, 100.0f}));
    CHECK(vec2_near(resolved.layout_origin, {200.0f, 100.0f}));
    CHECK(resolved.local_frame.x == doctest::Approx(200.0f));
    CHECK(resolved.local_frame.y == doctest::Approx(100.0f));

    const Vec4 local_origin =
        resolved.world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(local_origin.x == doctest::Approx(200.0f));
    CHECK(local_origin.y == doctest::Approx(100.0f));
}

TEST_CASE("CoordinateContract: anchor and box size determine box origin") {
    const CanvasInfo canvas = default_canvas();
    const TextPlacement placement{
        TextPlacementKind::Absolute, {200.0f, 100.0f}};

    const auto centered = resolve_text_placement(
        canvas, Vec2{400.0f, 100.0f}, placement, TextAnchor::Center);
    const auto top_left = resolve_text_placement(
        canvas, Vec2{400.0f, 100.0f}, placement, TextAnchor::TopLeft);
    const auto resized = resolve_text_placement(
        canvas, Vec2{100.0f, 50.0f}, placement, TextAnchor::Center);

    // Center anchor subtracts half the box from the Canvas pin.
    CHECK(vec2_near(centered.layout_origin, {0.0f, 50.0f}));
    // TopLeft anchor has no anchor offset.
    CHECK(vec2_near(top_left.layout_origin, {200.0f, 100.0f}));
    // Changing box size changes the origin, but not the Absolute pin.
    CHECK(vec2_near(resized.layout_origin, {150.0f, 75.0f}));
    CHECK(vec2_near(resolve_placement_origin(canvas, Vec2{100.0f, 50.0f}, placement),
                    {200.0f, 100.0f}));
}

TEST_CASE("CoordinateContract: layer matrix composes after Canvas placement") {
    const CanvasInfo canvas = default_canvas();
    const TextPlacement placement{
        TextPlacementKind::Absolute, {200.0f, 100.0f}};
    const Mat4 layer_matrix = glm::translate(
        Mat4(1.0f), Vec3{30.0f, 40.0f, 0.0f});

    const auto resolved = resolve_text_placement(
        canvas, Vec2{400.0f, 100.0f}, placement,
        TextAnchor::TopLeft, layer_matrix);
    const Mat4 expected = layer_matrix * glm::translate(
        Mat4(1.0f), Vec3{200.0f, 100.0f, 0.0f});

    CHECK(mat4_near(resolved.world_matrix, expected));
    // The resolver's layout fields remain authoring Canvas coordinates;
    // the parent transform belongs only to world_matrix composition.
    CHECK(vec2_near(resolved.layout_origin, {200.0f, 100.0f}));
}

TEST_CASE("CoordinateContract: layer pin applies Canvas anchor and offset once") {
    Scene scene;
    Layer layer{};
    layer.name = "pinned";
    layer.layout.enabled = true;
    layer.layout.pin = AnchorPlacement{
        Anchor::Center, Vec2{30.0f, -20.0f}, 7.0f};
    scene.layers().push_back(layer);

    LayoutSolver{}.solve(scene, 1920, 1080);
    REQUIRE(scene.layers().size() == 1);

    const Layer& resolved = scene.layers().front();
    // Canvas center (960, 540) plus the layer-local pin offset.
    CHECK(resolved.transform.position.x == doctest::Approx(990.0f));
    CHECK(resolved.transform.position.y == doctest::Approx(520.0f));
    CHECK(resolved.transform.position.z == doctest::Approx(7.0f));
}

namespace {

Layer materialize_coordinate_test_layer(
    TextPlacement placement,
    TextAnchor anchor,
    Vec2 box,
    std::optional<AnchorPlacement> layer_pin = std::nullopt)
{
    LayerBuilder builder("coordinate_contract", SampleTime{});
    builder.screen_dimensions(1920.0f, 1080.0f);
    if (layer_pin) {
        builder.pin_to(*layer_pin);
    }

    PreparedText prepared;
    prepared.document.utf8 = "coordinate contract";
    prepared.frame.size = box;
    prepared.frame.placement = placement;
    prepared.frame.anchor = anchor;
    (void)builder.text_run("text", std::move(prepared));
    return builder.build();
}

const RenderNode& only_text_node(const Layer& layer) {
    REQUIRE(layer.nodes.size() == 1);
    return layer.nodes.front();
}

} // namespace

TEST_CASE("CoordinateContract: LayerBuilder unpinned Absolute uses Canvas-to-render basis once") {
    const Layer layer = materialize_coordinate_test_layer(
        TextPlacement{TextPlacementKind::Absolute, {200.0f, 100.0f}},
        TextAnchor::TopLeft,
        Vec2{400.0f, 100.0f});
    const auto& node = only_text_node(layer);

    // ADR-019 basis contract: the 2D graph basis is centered at (0,0), so
    // the builder converts the Canvas pin exactly once (pin − canvas-half)
    // and layer_resolver.cpp adds +canvas-half back for unpinned layers.
    // Assert the centered-basis node position AND that the round trip
    // through the layer shift restores the Canvas pin — catching both a
    // missing conversion (double shift downstream) and a double one here.
    CHECK(node.world_transform.position.x == doctest::Approx(200.0f - 960.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(100.0f - 540.0f));
    CHECK(node.world_transform.anchor == Vec3{0.0f, 0.0f, 0.0f});
    CHECK(node.world_transform.position.x + 960.0f == doctest::Approx(200.0f));
    CHECK(node.world_transform.position.y + 540.0f == doctest::Approx(100.0f));
}

TEST_CASE("CoordinateContract: LayerBuilder unpinned Center resolves anchor and box size") {
    const Layer layer = materialize_coordinate_test_layer(
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextAnchor::Center,
        Vec2{400.0f, 100.0f});
    const auto& node = only_text_node(layer);

    // Box origin = (960,540) - (200,50), then Canvas-to-render conversion.
    CHECK(node.world_transform.position.x == doctest::Approx(-200.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(-50.0f));
    CHECK(node.world_transform.anchor == Vec3{0.0f, 0.0f, 0.0f});
}

TEST_CASE("CoordinateContract: LayerBuilder pinned Absolute is a local offset") {
    const Layer layer = materialize_coordinate_test_layer(
        TextPlacement{TextPlacementKind::Absolute, {30.0f, 20.0f}},
        TextAnchor::TopLeft,
        Vec2{400.0f, 100.0f},
        AnchorPlacement{Anchor::Center});
    const auto& node = only_text_node(layer);

    // The layer owns (960,540); the TextRun carries only the local {30,20}.
    CHECK(node.world_transform.position.x == doctest::Approx(30.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(20.0f));
    CHECK(node.world_transform.anchor == Vec3{0.0f, 0.0f, 0.0f});
}

TEST_CASE("CoordinateContract: LayerBuilder pinned Absolute center keeps box anchor local") {
    const Layer layer = materialize_coordinate_test_layer(
        TextPlacement{TextPlacementKind::Absolute, {30.0f, 20.0f}},
        TextAnchor::Center,
        Vec2{400.0f, 100.0f},
        AnchorPlacement{Anchor::Center});
    const auto& node = only_text_node(layer);

    // Local origin = offset {30,20} - anchor offset {200,50}.
    CHECK(node.world_transform.position.x == doctest::Approx(-170.0f));
    CHECK(node.world_transform.position.y == doctest::Approx(-30.0f));
}
