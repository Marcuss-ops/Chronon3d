#include <doctest/doctest.h>

#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <memory>
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
using chronon3d::graph::detail::LayerStructure;
using chronon3d::graph::detail::LayerCamera;
using chronon3d::graph::detail::LayerPosition;
using chronon3d::graph::detail::LayerOpacity;
using chronon3d::graph::detail::LayerText;
using chronon3d::graph::detail::LayerColor;
using chronon3d::graph::detail::LayerImage;
using chronon3d::graph::detail::LayerEffects;
using chronon3d::graph::detail::LayerVideoSource;
using chronon3d::SemanticText;
using chronon3d::SemanticColor;
using chronon3d::SemanticImage;
using chronon3d::SemanticEffects;
using chronon3d::SemanticVideoSource;

TEST_CASE("FrameExecutionPlan carries the complete execution contract") {
    chronon3d::graph::FrameExecutionPlan plan;
    plan.path = chronon3d::graph::FrameExecutionPath::SparseTiles;
    plan.decode = true;
    plan.render = true;
    plan.composite = true;
    plan.convert_to_rgb = true;
    plan.convert_to_yuv = false;
    plan.encode = true;
    plan.dirty_region = BBox{4, 8, 20, 24};
    plan.reason = "sparse_tiles";
    plan.output_surface = std::make_shared<chronon3d::runtime::CpuRgbSurface>(
        std::make_shared<chronon3d::Framebuffer>(32, 16));

    CHECK(plan.path == chronon3d::graph::FrameExecutionPath::SparseTiles);
    CHECK(plan.decode);
    CHECK(plan.render);
    CHECK(plan.composite);
    CHECK(plan.convert_to_rgb);
    CHECK_FALSE(plan.convert_to_yuv);
    CHECK(plan.encode);
    REQUIRE(plan.dirty_region.has_value());
    CHECK(plan.dirty_region->x0 == 4);
    CHECK(plan.reason == "sparse_tiles");
    REQUIRE(plan.output_surface != nullptr);
    CHECK(plan.output_surface->kind() ==
          chronon3d::runtime::RenderSurfaceKind::CpuRgb);
    CHECK(plan.output_surface->valid());
}

TEST_CASE("RenderSurface contracts preserve CPU fallback and typed formats") {
    using namespace chronon3d::runtime;

    auto framebuffer = std::make_shared<chronon3d::Framebuffer>(8, 4);
    CpuRgbSurface cpu(framebuffer);
    CHECK(cpu.kind() == RenderSurfaceKind::CpuRgb);
    CHECK(cpu.valid());
    CHECK(cpu.handle() == kInvalidRenderSurfaceHandle);
    CHECK(cpu.cpu_framebuffer() == framebuffer.get());
    CHECK(cpu.desc().format == PixelFormat::Rgba32Float);
    CHECK(cpu.desc().width == 8);
    CHECK(cpu.desc().height == 4);

    const SurfaceDesc rgb_desc{
        8, 4, PixelFormat::Rgba8Unorm, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    GpuRgbSurface gpu_rgb(11, rgb_desc);
    CHECK(gpu_rgb.kind() == RenderSurfaceKind::GpuRgb);
    CHECK(gpu_rgb.valid());
    CHECK(gpu_rgb.handle() == 11);
    CHECK(gpu_rgb.cpu_framebuffer() == nullptr);

    const SurfaceDesc yuv_desc{
        8, 4, PixelFormat::Nv12, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    GpuYuvSurface gpu_yuv(12, yuv_desc);
    CHECK(gpu_yuv.kind() == RenderSurfaceKind::GpuYuv);
    CHECK(gpu_yuv.valid());
    CHECK(gpu_yuv.desc().bytes == tight_surface_bytes(PixelFormat::Nv12, 8, 4));

    ExternalSurface external(13, rgb_desc, 99);
    CHECK(external.kind() == RenderSurfaceKind::External);
    CHECK(external.valid());
    CHECK(external.external_token() == 99);

    const SurfaceDesc odd_yuv_desc{
        7, 4, PixelFormat::Nv12, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    CHECK_FALSE(GpuYuvSurface(14, odd_yuv_desc).valid());
    CHECK_FALSE(GpuRgbSurface(15, yuv_desc).valid());
}

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

TEST_CASE("ExecutionResolver coalesces sparse dirty tiles into execution regions") {
    const TileGrid grid(64, 48, 16);
    chronon3d::raster::DirtyTileMask mask(grid);
    mask.mark_tile(0, 0);
    mask.mark_tile(1, 0);
    mask.mark_tile(0, 1);
    mask.mark_tile(1, 1);
    mask.mark_tile(3, 2);

    const auto regions = chronon3d::graph::ExecutionResolver::coalesce_dirty_regions(
        grid, mask);

    REQUIRE(regions.size() == 2);
    CHECK(regions[0].x0 == 0);
    CHECK(regions[0].y0 == 0);
    CHECK(regions[0].x1 == 32);
    CHECK(regions[0].y1 == 32);
    CHECK(regions[1].x0 == 48);
    CHECK(regions[1].y0 == 32);
    CHECK(regions[1].x1 == 64);
    CHECK(regions[1].y1 == 48);
}

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

TEST_CASE("FrameDeltaCompiler classifies semantic changes") {
    auto previous_layer = layer(BBox{10, 10, 30, 30});
    auto current_layer = previous_layer;
    previous_layer.uses_2_5d_projection = true;
    current_layer.uses_2_5d_projection = true;
    current_layer.world_matrix[3][0] = 4.0f;
    current_layer.world_matrix[0][0] = 2.0f;
    current_layer.opacity = 0.5f;
    current_layer.visible = false;
    current_layer.content_hash = 99;

    constexpr auto all_semantics = SemanticText | SemanticColor | SemanticImage |
        SemanticEffects | SemanticVideoSource;
    previous_layer.semantic_fingerprints_valid = true;
    current_layer.semantic_fingerprints_valid = true;
    previous_layer.semantic_presence = all_semantics;
    current_layer.semantic_presence = all_semantics;
    previous_layer.structure_hash = 1;
    current_layer.structure_hash = 2;
    previous_layer.text_hash = 1;
    current_layer.text_hash = 2;
    previous_layer.color_hash = 1;
    current_layer.color_hash = 2;
    previous_layer.image_hash = 1;
    current_layer.image_hash = 2;
    previous_layer.effects_hash = 1;
    current_layer.effects_hash = 2;
    previous_layer.video_source_hash = 1;
    current_layer.video_source_hash = 2;

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{11},
        {{"semantic", current_layer}},
        {{"semantic", previous_layer}},
        true,
        100,
        100);

    REQUIRE(delta.changes.size() == 1);
    const auto mask = delta.changes.front().change_mask;
    CHECK((mask & LayerStructure) != 0);
    CHECK((mask & LayerCamera) != 0);
    CHECK((mask & LayerPosition) != 0);
    CHECK((mask & LayerOpacity) != 0);
    CHECK((mask & LayerText) != 0);
    CHECK((mask & LayerColor) != 0);
    CHECK((mask & LayerImage) != 0);
    CHECK((mask & LayerEffects) != 0);
    CHECK((mask & LayerVideoSource) != 0);
    CHECK(delta.scene_changed);
    CHECK(delta.camera_changed);
    CHECK(delta.structure_changed);
    CHECK(delta.geometry_changed);
    CHECK(delta.content_changed);
    CHECK(delta.visibility_changed);
    CHECK(delta.position_changed);
    CHECK(delta.opacity_changed);
    CHECK(delta.text_changed);
    CHECK(delta.color_changed);
    CHECK(delta.image_changed);
    CHECK(delta.effects_changed);
    CHECK(delta.video_source_changed);
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

namespace {

chronon3d::graph::detail::FrameStateSnapshot reuse_state(
    chronon3d::Frame frame,
    std::uint64_t static_fp,
    std::uint64_t active_at_fp,
    std::uint64_t structure_fp,
    std::uint64_t combined_fp) {
    chronon3d::graph::detail::FrameStateSnapshot state;
    state.frame = frame;
    state.fingerprints = chronon3d::graph::FrameFingerprints{
        static_fp, active_at_fp, structure_fp, combined_fp};
    state.fingerprints_valid = true;
    state.has_previous_surface = true;
    return state;
}

} // namespace

TEST_CASE("FrameDeltaCompiler grants resolved reuse for identical sequential states") {
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);

    const auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);

    CHECK(delta.reuse.resolved_scene_reuse);
    CHECK_FALSE(delta.reuse.static_scene_reuse);
    CHECK(delta.reuse.structure_unchanged);
    CHECK(delta.reuse.camera_unchanged);
    CHECK(delta.reuse.reason.empty());
}

TEST_CASE("FrameDeltaCompiler rejects reuse when fingerprints or camera change") {
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 99);

    auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);
    CHECK_FALSE(delta.reuse.resolved_scene_reuse);
    CHECK(delta.reuse.reason == "combined_fingerprint_changed");

    current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    current.camera.enabled = true;
    current.camera.position.x = 10.0f;
    delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);
    CHECK_FALSE(delta.reuse.resolved_scene_reuse);
    CHECK_FALSE(delta.reuse.camera_unchanged);
    CHECK(delta.reuse.reason == "camera_changed");
}

TEST_CASE("FrameDeltaCompiler reports static-scene reuse independently of resolved reuse") {
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    current.scene_is_static = true;

    const auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);

    CHECK(delta.reuse.resolved_scene_reuse);
    CHECK(delta.reuse.static_scene_reuse);
}

TEST_CASE("FrameDeltaCompiler exposes semantic layer changes through the full state entry") {
    auto previous_layer = layer(BBox{4, 8, 20, 24});
    auto current_layer = previous_layer;
    previous_layer.semantic_fingerprints_valid = true;
    current_layer.semantic_fingerprints_valid = true;
    previous_layer.semantic_presence = SemanticText | SemanticColor;
    current_layer.semantic_presence = SemanticText | SemanticColor;
    previous_layer.text_hash = 1;
    current_layer.text_hash = 2;
    previous_layer.color_hash = 3;
    current_layer.color_hash = 4;

    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    previous.layers.emplace("caption", previous_layer);
    current.layers.emplace("caption", current_layer);

    const auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);

    REQUIRE(delta.changes.size() == 1);
    const auto mask = delta.changes.front().change_mask;
    CHECK((mask & LayerText) != 0);
    CHECK((mask & LayerColor) != 0);
    CHECK(delta.text_changed);
    CHECK(delta.color_changed);
    REQUIRE(delta.dirty_bounds.has_value());
    CHECK_FALSE(delta.dirty_bounds->is_empty());
}

TEST_CASE("FrameDeltaCompiler blocks reuse for projected surfaces and missing surfaces") {
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    current.has_projected_surface = true;

    auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);
    CHECK_FALSE(delta.reuse.resolved_scene_reuse);
    CHECK(delta.reuse.reason == "projected_surface");

    current.has_projected_surface = false;
    current.has_previous_surface = false;
    delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);
    CHECK_FALSE(delta.reuse.resolved_scene_reuse);
    CHECK(delta.reuse.reason == "missing_previous_surface");
}
