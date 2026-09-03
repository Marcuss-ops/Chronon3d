#include <doctest/doctest.h>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>

#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <memory>
#include <type_traits>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
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
using chronon3d::graph::CompiledFrameGraph;
using chronon3d::graph::FrameParameterTable;
using chronon3d::graph::FrameParameterWriter;
using chronon3d::graph::ParameterPatch;
using chronon3d::graph::ParameterPatchSet;

TEST_CASE("CompiledFrameGraph applies ParameterPatchSet without topology changes") {
    CompiledFrameGraph compiled;
    auto table = std::make_shared<FrameParameterTable>();
    table->warm_up(1, sizeof(std::uint32_t));
    table->sample(chronon3d::Frame{0}, [](FrameParameterWriter& writer) {
        const std::uint32_t value = 1;
        writer.write(value);
    });
    compiled.prepared_parameters = table;
    compiled.nodes.resize(1);
    compiled.nodes[0].name = "stable-node";
    compiled.nodes[0].reachable = true;

    const auto original_name = compiled.nodes[0].name;
    ParameterPatchSet patches;
    patches.patches.push_back(ParameterPatch{
        .slot_id = 0,
        .offset = 0,
        .value = {std::byte{0x2A}, std::byte{0}, std::byte{0}, std::byte{0}}});
    compiled.apply_parameter_patches(patches);

    CHECK(compiled.nodes[0].name == original_name);
    const auto bytes = compiled.prepared_parameters->bytes(0, sizeof(std::uint32_t));
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof(value));
    CHECK(value == 42u);
}
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

TEST_CASE("ExecutionDecision propagates through the canonical command plan") {
    chronon3d::runtime::GpuCommandPlanner planner;
    const chronon3d::graph::ExecutionDecision decision{
        chronon3d::graph::FrameExecutionPath::ReuseSurface, "reuse_surface"};
    planner.set_execution_decision(decision);

    const auto command_plan = planner.build();

    REQUIRE(command_plan.execution_decision.has_value());
    CHECK(command_plan.execution_decision->reuses_surface());
    CHECK(command_plan.execution_decision->reason == "reuse_surface");
}

TEST_CASE("ExecutionDecision propagates through CompiledFrameGraph") {
    chronon3d::graph::CompiledFrameGraph compiled;
    compiled.execution_decision = chronon3d::graph::ExecutionDecision{
        chronon3d::graph::FrameExecutionPath::FullRgb, "scene_changed"};

    REQUIRE(compiled.execution_decision.has_value());
    CHECK(compiled.execution_decision->renders_full_rgb());
    CHECK(compiled.execution_decision->reason == "scene_changed");
}

TEST_CASE("YUV command plan preserves NV12 and P010 format metadata") {
    chronon3d::runtime::GpuCommandPlanner planner;
    planner.yuv_overlay({1, 2, chronon3d::runtime::PixelFormat::Nv12,
                         BBox{0, 0, 16, 16},
                         chronon3d::runtime::YuvExecutionMode::Sparse});
    const auto nv12 = planner.build();
    REQUIRE(nv12.passes.passes.size() == 1);
    const auto& nv12_pass = std::get<chronon3d::runtime::YuvOverlayPass>(
        nv12.passes.passes.front().params);
    CHECK(nv12_pass.format == chronon3d::runtime::PixelFormat::Nv12);
    CHECK(nv12_pass.mode == chronon3d::runtime::YuvExecutionMode::Sparse);

    chronon3d::runtime::GpuCommandPlanner p010_planner;
    p010_planner.yuv_overlay({3, 4, chronon3d::runtime::PixelFormat::P010,
                              std::nullopt,
                              chronon3d::runtime::YuvExecutionMode::Full});
    const auto p010 = p010_planner.build();
    const auto& p010_pass = std::get<chronon3d::runtime::YuvOverlayPass>(
        p010.passes.passes.front().params);
    CHECK(p010_pass.format == chronon3d::runtime::PixelFormat::P010);
    CHECK(p010_pass.mode == chronon3d::runtime::YuvExecutionMode::Full);
}

TEST_CASE("ExecutionResolver resolves YUV execution paths from a FrameDelta") {
    chronon3d::graph::detail::DirtyRectOutput dirty;
    dirty.frame_delta = chronon3d::graph::detail::FrameDelta{};
    dirty.frame_delta->scene_changed = true;
    dirty.use_dirty_tiles = true;
    dirty.tile_grid = chronon3d::raster::TileGrid(32, 32, 16);
    dirty.dirty_tiles = chronon3d::raster::DirtyTileMask(*dirty.tile_grid);
    dirty.dirty_tiles->mark_tile(0, 0);
    dirty.dirty_rect = BBox{0, 0, 16, 16};

    auto plan = chronon3d::graph::ExecutionResolver::resolve(
        {}, {}, chronon3d::Scene{}, chronon3d::Camera2_5D{},
        chronon3d::RenderSettings{}, dirty, 0.25, nullptr,
        chronon3d::Frame{1}, 32, 32, nullptr, false, false,
        chronon3d::runtime::PixelFormat::Nv12);
    CHECK(plan.path == chronon3d::graph::FrameExecutionPath::SparseYuv);
    CHECK(plan.reason == "sparse_yuv_nv12");
    REQUIRE(plan.dirty_regions.size() == 1);

    dirty.use_dirty_tiles = false;
    plan = chronon3d::graph::ExecutionResolver::resolve(
        {}, {}, chronon3d::Scene{}, chronon3d::Camera2_5D{},
        chronon3d::RenderSettings{}, dirty, 1.0, nullptr,
        chronon3d::Frame{1}, 32, 32, nullptr, false, false,
        chronon3d::runtime::PixelFormat::P010);
    CHECK(plan.path == chronon3d::graph::FrameExecutionPath::FullYuv);
    CHECK(plan.reason == "full_yuv_p010");
}

TEST_CASE("ExecutionResolver resolves initial ReuseSurface from a clean FrameDelta") {
    chronon3d::graph::detail::FrameDelta delta;
    delta.reuse.resolved_scene_reuse = true;

    const auto decision = chronon3d::graph::ExecutionResolver::resolve_initial(delta);

    CHECK(decision.path == chronon3d::graph::FrameExecutionPath::ReuseSurface);
    CHECK(decision.reuses_surface());
    CHECK_FALSE(decision.renders_full_rgb());
    CHECK(decision.reason == "reuse_surface");
}

TEST_CASE("ExecutionResolver resolves changed FrameDelta to FullRgb") {
    chronon3d::graph::detail::FrameDelta delta;
    delta.scene_changed = true;
    delta.content_changed = true;
    delta.reuse.reason = "layer_delta_present";

    const auto decision = chronon3d::graph::ExecutionResolver::resolve_initial(delta);

    CHECK(decision.path == chronon3d::graph::FrameExecutionPath::FullRgb);
    CHECK_FALSE(decision.reuses_surface());
    CHECK(decision.renders_full_rgb());
    CHECK(decision.reason == "scene_changed");
}

TEST_CASE("ExecutionResolver fails closed to FullRgb when reuse is not eligible") {
    chronon3d::graph::detail::FrameDelta delta;
    delta.reuse.reason = "missing_previous_surface";

    const auto decision = chronon3d::graph::ExecutionResolver::resolve_initial(delta);

    CHECK(decision.path == chronon3d::graph::FrameExecutionPath::FullRgb);
    CHECK(decision.reason == "missing_previous_surface");
}

TEST_CASE("ExecutionResolver is the sole canonical frame-path resolver") {
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
    const TileGrid grid(128, 128, 16);

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{8}, current, previous, false, 128, 128, &grid);

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

TEST_CASE("FrameDeltaCompiler propagates spatial damage spread to bounds and tiles") {
    const TileGrid grid(128, 128, 16);
    auto previous_layer = layer(BBox{48, 48, 64, 64});
    auto current_layer = previous_layer;
    current_layer.content_hash = 2;

    chronon3d::graph::detail::FrameDeltaCompileOptions options;
    options.full_frame_threshold = 0.0;
    options.spatial_spread = [](std::string_view id) {
        return id == "logo" ? 8.0 : 0.0;
    };

    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{10},
        {{"logo", current_layer}},
        {{"logo", previous_layer}},
        false, 128, 128, &grid, options);

    REQUIRE(delta.dirty_bounds.has_value());
    CHECK(delta.dirty_bounds->x0 == 40);
    CHECK(delta.dirty_bounds->y0 == 40);
    CHECK(delta.dirty_bounds->x1 == 72);
    CHECK(delta.dirty_bounds->y1 == 72);
    REQUIRE(delta.dirty_tiles.has_value());
    CHECK(delta.dirty_tiles->dirty_count() == 9);
}

TEST_CASE("FrameDeltaCompiler normalizes full-frame damage and tiles") {
    const TileGrid grid(64, 64, 16);
    const auto delta = FrameDeltaCompiler::compile(
        chronon3d::Frame{10}, {}, {}, false, 64, 64, &grid,
        chronon3d::graph::detail::FrameDeltaCompileOptions{.force_full_frame = true});

    REQUIRE(delta.dirty_bounds.has_value());
    CHECK(delta.full_frame_dirty);
    CHECK(delta.dirty_bounds->x0 == 0);
    CHECK(delta.dirty_bounds->y0 == 0);
    CHECK(delta.dirty_bounds->x1 == 64);
    CHECK(delta.dirty_bounds->y1 == 64);
    REQUIRE(delta.dirty_tiles.has_value());
    CHECK(delta.dirty_tiles->dirty_count() == grid.tile_count());
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
    state.layer_state_complete = false;
    return state;
}

} // namespace

TEST_CASE("FrameDeltaCompiler grants resolved reuse for identical sequential states") {
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    current.scene_is_static = true;

    const auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);

    CHECK(delta.reuse.resolved_scene_reuse);
    CHECK_FALSE(delta.reuse.static_scene_reuse);
    CHECK(delta.reuse.structure_unchanged);
    CHECK(delta.reuse.camera_unchanged);
    CHECK(delta.reuse.reason.empty());
}

TEST_CASE("FrameDeltaCompiler rejects reuse when a layer delta is present") {
    auto previous_layer = layer(BBox{4, 8, 20, 24});
    auto current_layer = previous_layer;
    current_layer.content_hash = 9;
    auto previous = reuse_state(chronon3d::Frame{10}, 11, 12, 13, 14);
    auto current = reuse_state(chronon3d::Frame{11}, 11, 12, 13, 14);
    previous.layers.emplace("caption", previous_layer);
    current.layers.emplace("caption", current_layer);

    const auto delta = FrameDeltaCompiler::compile_state(previous, current, 64, 64);
    CHECK_FALSE(delta.reuse.resolved_scene_reuse);
    CHECK(delta.reuse.reason == "layer_delta_present");
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
    previous.layer_state_complete = true;
    current.layer_state_complete = true;

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
