#include <doctest/doctest.h>

#include <chronon3d/runtime/dirty_history.hpp>
#include <type_traits>
#include "../../../src/render_graph/pipeline/frame_delta_compiler.hpp"
#include "../../../src/render_graph/pipeline/tile_execution_policy.hpp"

using chronon3d::LayerBBoxState;
using chronon3d::Mat4;
using chronon3d::raster::BBox;
using chronon3d::raster::TileGrid;
using chronon3d::graph::detail::FrameDeltaCompiler;
using chronon3d::graph::detail::LayerAdded;
using chronon3d::graph::detail::LayerContent;
using chronon3d::graph::detail::LayerGeometry;
using chronon3d::graph::detail::LayerRemoved;

TEST_CASE("ExecutionResolver is the sole canonical frame-path resolver") {
    static_assert(std::is_same_v<chronon3d::graph::ExecutionResolver,
                                 chronon3d::graph::TileExecutionPolicy>);
    const chronon3d::graph::TileDecision decision{
        true, chronon3d::graph::FrameExecutionPath::SparseTiles,
        false, true, false, {}};
    CHECK(decision.path == chronon3d::graph::FrameExecutionPath::SparseTiles);
    CHECK(decision.enabled);
}

namespace {

LayerBBoxState layer(BBox bounds) {
    LayerBBoxState state;
    state.bbox = bounds;
    state.world_matrix = Mat4{1.0f};
    state.visible = true;
    state.cache_static = true;
    return state;
}

} // namespace

TEST_CASE("FrameDeltaCompiler unions old and new bounds for movement") {
    std::unordered_map<std::string, LayerBBoxState> previous{
        {"title", layer(BBox{10, 20, 30, 40})}};
    auto current_layer = layer(BBox{40, 20, 60, 40});
    current_layer.world_matrix[3][0] = 1.0f;
    std::unordered_map<std::string, LayerBBoxState> current{
        {"title", current_layer}};

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{7}, current, previous, false, 100, 100);

    REQUIRE(delta.changes.size() == 1);
    CHECK((delta.changes.front().change_mask & LayerGeometry) != 0);
    REQUIRE(delta.dirty_bounds.has_value());
    CHECK(delta.dirty_bounds->x0 == 10);
    CHECK(delta.dirty_bounds->x1 == 60);
}

TEST_CASE("FrameDeltaCompiler marks additions, removals, and tiles") {
    std::unordered_map<std::string, LayerBBoxState> previous{
        {"old", layer(BBox{0, 0, 16, 16})}};
    std::unordered_map<std::string, LayerBBoxState> current{
        {"new", layer(BBox{32, 32, 48, 48})}};
    const TileGrid grid(64, 64, 16);

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{8}, current, previous, false, 64, 64, &grid);

    REQUIRE(delta.changes.size() == 2);
    CHECK((delta.changes[0].change_mask & LayerAdded) != 0);
    CHECK((delta.changes[1].change_mask & LayerRemoved) != 0);
    REQUIRE(delta.dirty_tiles.has_value());
    CHECK(delta.dirty_tiles->dirty_count() == 2);
}

TEST_CASE("FrameDeltaCompiler reports content changes without geometry changes") {
    auto previous_layer = layer(BBox{10, 10, 30, 30});
    auto current_layer = previous_layer;
    current_layer.content_hash = 42;
    std::unordered_map<std::string, LayerBBoxState> previous{
        {"caption", previous_layer}};
    std::unordered_map<std::string, LayerBBoxState> current{
        {"caption", current_layer}};

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{9}, current, previous, false, 64, 64);

    REQUIRE(delta.changes.size() == 1);
    CHECK((delta.changes.front().change_mask & LayerContent) != 0);
    CHECK(delta.changes.front().old_bounds.x0 == 10);
    CHECK(delta.changes.front().new_bounds.x1 == 30);
}

TEST_CASE("FrameDeltaCompiler keeps an unchanged frame clean") {
    const auto unchanged = layer(BBox{4, 8, 20, 24});
    std::unordered_map<std::string, LayerBBoxState> previous{{"plate", unchanged}};
    std::unordered_map<std::string, LayerBBoxState> current{{"plate", unchanged}};
    const TileGrid grid(64, 64, 16);

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{10}, current, previous, false, 64, 64, &grid);

    CHECK(delta.changes.empty());
    REQUIRE(delta.dirty_bounds.has_value());
    CHECK(delta.dirty_bounds->is_empty());
    REQUIRE(delta.dirty_tiles.has_value());
    CHECK(delta.dirty_tiles->dirty_count() == 0);
}
