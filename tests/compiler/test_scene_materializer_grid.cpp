#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Procedural grid geometry at the materializer boundary.
//
// The render-plan path generators (make_grid_path / make_dot_grid_path) have
// their own coverage in tests/c_abi. This file pins the *scene* geometry the
// clip grid produces, which is what the compositor actually draws:
//
//   • whole number of cells, so the mesh closes on all four edges;
//   • border stroke inset by line_width / 2, so no line is cut in half;
//   • stroke centres snapped to whole pixels, so a 1 px line is one full pixel
//     instead of two at half opacity;
//   • RectParams.pos is the CENTRE of the rectangle (the factory anchors at
//     size / 2), so a full-height line sits at (x, height / 2);
//   • dot grids capped at kMaxGeneratedGridElements and radius <= half a cell.
// ============================================================================

namespace {

using namespace chronon3d;

constexpr float kCanvasWidth = 1920.0f;
constexpr float kCanvasHeight = 1080.0f;

render_plan::RenderPlan grid_plan(render_plan::ShapeContentTypePlan type,
                                  float spacing, float line_width, float dot_radius) {
    render_plan::RenderPlan plan;
    plan.job_id = "materializer-grid";
    plan.canvas = {.width = static_cast<int>(kCanvasWidth),
                   .height = static_cast<int>(kCanvasHeight),
                   .fps = FrameRate{24, 1},
                   .duration = Frame{1}};
    plan.output.path = "materializer-grid.mp4";

    render_plan::LayerPlan layer;
    layer.id = "grid";
    layer.type = render_plan::LayerType::Shape;
    layer.size = {kCanvasWidth, kCanvasHeight};
    layer.size_dimensions = 2;

    render_plan::ShapeContentPlan shape;
    shape.type = type;
    shape.grid_spacing = spacing;
    shape.grid_line_width = line_width;
    shape.dot_radius = dot_radius;
    layer.shape = shape;

    plan.layers.push_back(std::move(layer));
    return plan;
}

Scene materialize(const render_plan::RenderPlan& plan) {
    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::current_path());
    auto compiled = compiler::compile(plan, resolver);
    REQUIRE(compiled);
    return compiler::SceneMaterializer{}.materialize_scene(
        std::move(compiled).value(), std::pmr::get_default_resource(), nullptr);
}

std::vector<const RenderNode*> nodes_with_prefix(const Scene& scene, std::string_view prefix) {
    std::vector<const RenderNode*> out;
    for (const auto& layer : scene.layers()) {
        for (const auto& node : layer.nodes) {
            if (std::string_view{node.name.data(), node.name.size()}.rfind(prefix, 0) == 0) {
                out.push_back(&node);
            }
        }
    }
    return out;
}

bool near(float actual, float expected, float tolerance = 1e-3f) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

TEST_CASE("SceneMaterializer lays the clip grid on a closed whole-cell mesh") {
    // 1919 / 72 = 26.65 -> 27 cells, 1079 / 72 = 14.99 -> 15 cells.
    // A raw spacing step left 48 px open on the right edge (last line at 1872)
    // and put the bottom line on the border instead of inside it.
    const Scene scene = materialize(grid_plan(render_plan::ShapeContentTypePlan::Grid,
                                              72.0f, 1.0f, 3.0f));

    const auto verticals = nodes_with_prefix(scene, "grid_v_");
    const auto horizontals = nodes_with_prefix(scene, "grid_h_");
    REQUIRE(verticals.size() == 28);
    REQUIRE(horizontals.size() == 16);

    const float line_width = 1.0f;
    const float inner_w = kCanvasWidth - line_width;   // 1919
    const float inner_h = kCanvasHeight - line_width;  // 1079
    const float inset = line_width * 0.5f;

    // First and last stroke keep their full width inside the box.
    CHECK(near(verticals.front()->world_transform.position.x, inset));
    CHECK(near(verticals.back()->world_transform.position.x, kCanvasWidth - inset));
    CHECK(near(horizontals.front()->world_transform.position.y, inset));
    CHECK(near(horizontals.back()->world_transform.position.y, kCanvasHeight - inset));

    // RectParams.pos is the centre: a full-height line is centred on the box.
    CHECK(near(verticals.front()->world_transform.position.y, kCanvasHeight * 0.5f));
    CHECK(near(horizontals.front()->world_transform.position.x, kCanvasWidth * 0.5f));

    // The stroke is shortened by the inset instead of overhanging the box.
    CHECK(near(verticals.front()->shape.rect().size.x, line_width));
    CHECK(near(verticals.front()->shape.rect().size.y, inner_h));
    CHECK(near(horizontals.front()->shape.rect().size.x, inner_w));
    CHECK(near(horizontals.front()->shape.rect().size.y, line_width));

    // Every stroke centre is snapped to a whole pixel: a 1 px line lands on one
    // full pixel instead of two half-opacity ones.
    for (const auto* node : verticals) {
        const float x = node->world_transform.position.x;
        CHECK(near(x - inset, std::round(x - inset), 1e-4f));
        CHECK(x >= inset - 1e-3f);
        CHECK(x <= kCanvasWidth - inset + 1e-3f);
    }
    for (const auto* node : horizontals) {
        const float y = node->world_transform.position.y;
        CHECK(near(y - inset, std::round(y - inset), 1e-4f));
        CHECK(y >= inset - 1e-3f);
        CHECK(y <= kCanvasHeight - inset + 1e-3f);
    }

    // Cells divide the usable box into a whole count: the ideal step is
    // inner/columns and inner/rows. Each stroke is then snapped to the pixel
    // grid, so a single gap may drift by up to half a pixel from the ideal,
    // but every centre must sit exactly on round(i * step) + inset.
    const float step_x = inner_w / 27.0f;
    const float step_y = inner_h / 15.0f;
    CHECK(step_x > 71.0f);
    CHECK(step_x < 72.0f);
    CHECK(step_y > 71.0f);
    CHECK(step_y < 72.0f);
    for (std::size_t i = 0; i < verticals.size(); ++i) {
        const float ideal = inset + std::round(static_cast<float>(i) * step_x);
        CHECK(near(verticals[i]->world_transform.position.x, ideal));
    }
    for (std::size_t i = 0; i < horizontals.size(); ++i) {
        const float ideal = inset + std::round(static_cast<float>(i) * step_y);
        CHECK(near(horizontals[i]->world_transform.position.y, ideal));
    }
}

TEST_CASE("SceneMaterializer centres and bounds every grid dot") {
    // 1920 / 64 = 30 columns, 1080 / 64 = 16.875 -> 17 rows.
    const Scene scene = materialize(grid_plan(render_plan::ShapeContentTypePlan::DotGrid,
                                              64.0f, 1.0f, 3.0f));

    const auto dots = nodes_with_prefix(scene, "dot_");
    REQUIRE(dots.size() == 30U * 17U);

    const float step_x = kCanvasWidth / 30.0f;   // 64
    const float step_y = kCanvasHeight / 17.0f;  // 63.529

    CHECK(near(dots.front()->world_transform.position.x, step_x * 0.5f, 1e-2f));
    CHECK(near(dots.front()->world_transform.position.y, step_y * 0.5f, 1e-2f));
    CHECK(near(dots.back()->world_transform.position.x, kCanvasWidth - step_x * 0.5f, 1e-2f));
    CHECK(near(dots.back()->world_transform.position.y, kCanvasHeight - step_y * 0.5f, 1e-2f));

    // Margins are symmetric: first dot to the left edge equals last dot to the
    // right edge, so no dot is clipped by the canvas border.
    CHECK(near(dots.front()->world_transform.position.x,
               kCanvasWidth - dots.back()->world_transform.position.x, 1e-2f));

    for (const auto* dot : dots) {
        CHECK(dot->shape.circle().radius > 0.0f);
        CHECK(dot->shape.circle().radius <= std::min(step_x, step_y) * 0.5f + 1e-3f);
        CHECK(dot->world_transform.position.x >= dot->shape.circle().radius - 1e-3f);
        CHECK(dot->world_transform.position.y >= dot->shape.circle().radius - 1e-3f);
        CHECK(dot->world_transform.position.x <= kCanvasWidth - dot->shape.circle().radius + 1e-3f);
        CHECK(dot->world_transform.position.y <= kCanvasHeight - dot->shape.circle().radius + 1e-3f);
    }
}

TEST_CASE("SceneMaterializer clamps a dot radius wider than half a cell") {
    // A radius above half the cell merges neighbouring dots into a smudge.
    const Scene scene = materialize(grid_plan(render_plan::ShapeContentTypePlan::DotGrid,
                                              64.0f, 1.0f, 100.0f));
    const auto dots = nodes_with_prefix(scene, "dot_");
    REQUIRE(!dots.empty());

    const float step_y = kCanvasHeight / 17.0f;
    const float max_radius = std::min(64.0f, step_y * 0.5f);
    for (const auto* dot : dots) {
        CHECK(dot->shape.circle().radius <= max_radius + 1e-3f);
    }
}

TEST_CASE("SceneMaterializer fails closed when a dot grid exceeds the primitive cap") {
    // 1920/4 x 1080/4 = 480 x 270 cells: 129.600 circles.
    CHECK_THROWS_AS(static_cast<void>(materialize(
                        grid_plan(render_plan::ShapeContentTypePlan::DotGrid, 4.0f, 1.0f, 1.0f))),
                    std::length_error);
}

TEST_CASE("SceneMaterializer keeps a dense line grid under the cap") {
    // The same spacing is fine for lines: 481 verticals + 271 horizontals.
    const Scene scene = materialize(grid_plan(render_plan::ShapeContentTypePlan::Grid,
                                              4.0f, 1.0f, 3.0f));
    CHECK(nodes_with_prefix(scene, "grid_v_").size() == 481);
    CHECK(nodes_with_prefix(scene, "grid_h_").size() == 271);
}
