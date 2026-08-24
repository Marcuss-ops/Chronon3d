#include "tile_execution_policy.hpp"

#include "camera_change_policy.hpp"
#include "scene_fingerprint.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

namespace chronon3d::graph {
namespace {

bool has_matching_previous_surface(
    const SoftwareRenderer* sw_renderer,
    int width,
    int height) noexcept {
    if (!sw_renderer || !sw_renderer->buffer_ring().prev_framebuffer()) {
        return false;
    }
    const auto& previous = sw_renderer->buffer_ring().prev_framebuffer();
    return previous->width() == width && previous->height() == height;
}

void record_surface_reuse(
    SoftwareRenderer* sw_renderer,
    Frame frame,
    const Camera2_5D& camera,
    std::uint64_t combined_fingerprint,
    int width,
    int height,
    bool diagnostics_enabled,
    const char* diagnostic_tag) {
    if (!sw_renderer) {
        return;
    }

    sw_renderer->mark_fast_path_reused(
        frame, camera, combined_fingerprint);

    if (sw_renderer->counters()) {
        sw_renderer->counters()->dirty_union_area_pixels.store(
            0, std::memory_order_relaxed);
        sw_renderer->counters()->clear_skipped_calls.fetch_add(
            1, std::memory_order_relaxed);
        sw_renderer->counters()->clear_skipped_pixels.fetch_add(
            static_cast<std::uint64_t>(width) * height,
            std::memory_order_relaxed);
    }

    if (diagnostics_enabled) {
        spdlog::info("[{}] frame={} surface_reuse=1",
                     diagnostic_tag, static_cast<int>(frame));
    }
}

void set_full_rgb(
    FrameExecutionPlan& plan,
    std::string reason,
    bool force_full_frame_clear = false) {
    plan.path = FrameExecutionPath::FullRgb;
    plan.decode = false;
    plan.render = true;
    plan.composite = true;
    plan.convert_to_rgb = false;
    plan.convert_to_yuv = false;
    plan.reason = std::move(reason);
    plan.use_dirty_region = false;
    plan.force_full_frame_clear = force_full_frame_clear;
    plan.output_surface.reset();
    plan.previous_surface.reset();
    plan.copy_previous_surface = false;
    plan.dirty_regions.clear();
    plan.reuse_surface.reset();
}

void set_reuse_surface(
    FrameExecutionPlan& plan,
    std::shared_ptr<Framebuffer> surface,
    std::string reason) {
    plan.path = FrameExecutionPath::ReuseSurface;
    plan.decode = false;
    plan.render = false;
    plan.composite = false;
    plan.convert_to_rgb = false;
    plan.convert_to_yuv = false;
    plan.output_surface = std::make_shared<runtime::CpuRgbSurface>(surface);
    plan.previous_surface.reset();
    plan.copy_previous_surface = false;
    plan.dirty_regions.clear();
    plan.reuse_surface = std::move(surface);
    plan.use_dirty_region = false;
    plan.reason = std::move(reason);
}

void apply_sparse_or_full_decision(
    FrameExecutionPlan& plan,
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    const SoftwareRenderer* sw_renderer,
    Frame frame,
    const effects::EffectCatalog* effect_catalog) {
    (void)settings;
    (void)dirty_ratio;

    // This is the sole sparse/full policy. The coordinator only executes the
    // path selected here.
    if (detail::has_layer_with_spatial_effects(resolved, frame, effect_catalog)) {
        set_full_rgb(plan, "spatial_effect_detected");
        return;
    }
    if (!dirty_out.use_dirty_tiles) {
        set_full_rgb(plan, "dirty_rects_not_active");
        return;
    }
    if (!sw_renderer || !sw_renderer->has_runtime()) {
        set_full_rgb(plan, "missing_renderer_runtime");
        return;
    }
    if (!dirty_out.tile_grid || !dirty_out.dirty_tiles ||
        !dirty_out.dirty_tiles->any()) {
        set_full_rgb(plan, "no_dirty_tiles");
        return;
    }
    if (!has_matching_previous_surface(sw_renderer, dirty_out.tile_grid->width(),
                                       dirty_out.tile_grid->height())) {
        set_full_rgb(plan, "missing_previous_surface");
        return;
    }

    plan.dirty_regions = ExecutionResolver::coalesce_dirty_regions(
        *dirty_out.tile_grid, *dirty_out.dirty_tiles);
    if (plan.dirty_regions.empty()) {
        set_full_rgb(plan, "no_dirty_regions");
        return;
    }
    plan.previous_surface = std::make_shared<runtime::CpuRgbSurface>(
        sw_renderer->buffer_ring().prev_framebuffer());
    plan.copy_previous_surface = true;

    const auto& cost = sw_renderer->dirty_telemetry();
    if (cost.tile_cost_model_ready() &&
        cost.tile_exec_ms_ewma > cost.full_frame_exec_ms_ewma * 1.10) {
        set_full_rgb(plan, "cost_model_tile_slower", true);
        return;
    }

    plan.path = FrameExecutionPath::SparseTiles;
    plan.decode = false;
    plan.render = true;
    plan.composite = true;
    plan.convert_to_rgb = false;
    plan.convert_to_yuv = false;
    plan.output_surface.reset();
    plan.use_dirty_region = true;
    plan.force_full_frame_clear = false;
    plan.reason = "sparse_tiles";
}

} // namespace

std::vector<raster::BBox> ExecutionResolver::coalesce_dirty_regions(
    const raster::TileGrid& grid,
    const raster::DirtyTileMask& mask) {
    std::vector<raster::BBox> regions;
    const int cols = grid.cols();
    const int rows = grid.rows();

    for (int ty = 0; ty < rows; ++ty) {
        int tx_min = cols;
        int tx_max = -1;
        for (int tx = 0; tx < cols; ++tx) {
            if (mask.is_dirty(tx, ty)) {
                tx_min = std::min(tx_min, tx);
                tx_max = std::max(tx_max, tx);
            }
        }
        if (tx_min > tx_max) continue;

        raster::BBox row_region = grid.tile_bounds(tx_min, ty);
        row_region.x1 = grid.tile_bounds(tx_max, ty).x1;
        if (!regions.empty()) {
            auto& previous = regions.back();
            if (previous.y1 == row_region.y0 &&
                previous.x0 == row_region.x0 &&
                previous.x1 == row_region.x1) {
                previous.y1 = row_region.y1;
                continue;
            }
        }
        regions.push_back(row_region);
    }
    return regions;
}

FrameExecutionPlan ExecutionResolver::resolve_early_reuse(
    const RenderGraphContext& ctx,
    const Scene& scene,
    Frame frame,
    int width,
    int height,
    SoftwareRenderer* sw_renderer) {
    FrameExecutionPlan plan;
    plan.reason = "awaiting_dirty_analysis";

    if (!sw_renderer) {
        plan.reason = "missing_renderer_runtime";
        return plan;
    }

    const Camera2_5D& camera = ctx.frame_input.camera_2_5d;
    const auto& history = sw_renderer->frame_history();
    const bool has_projected_surface = std::any_of(
        scene.layers().begin(), scene.layers().end(),
        [frame](const Layer& layer) {
            return layer.active_at(frame) &&
                   (layer.uses_2_5d_projection || layer.is_native_3d());
        });

    // Compute the fingerprints once and carry them in the plan.  This
    // replaces the former coordinator's separate reuse-evaluation result.
    plan.frame_fingerprints = compute_frame_fingerprints(
        sw_renderer->scene_hasher(), scene, frame,
        ctx.services.registry_generation);

    const bool has_surface = has_matching_previous_surface(sw_renderer, width, height);
    const bool frame_eligible = has_surface &&
        (history.prev_frame == frame - 1 || history.prev_frame == frame);
    const bool camera_moved = detail::camera_changed(
        camera, &history.prev_camera, history.prev_camera_valid);

    // Fast path #1: resolved-scene reuse. This intentionally retains the
    // previous strict contract based on the combined fingerprint.
    if (!has_projected_surface && frame_eligible && !camera_moved &&
        history.prev_scene_fingerprint == plan.frame_fingerprints.combined_fp) {
        record_surface_reuse(
            sw_renderer, frame, camera,
            plan.frame_fingerprints.combined_fp,
            width, height, ctx.policy.diagnostics_enabled,
            "resolved-reuse");
        set_reuse_surface(
            plan, sw_renderer->buffer_ring().prev_framebuffer(),
            "resolved_scene_reuse");
        plan.dirty_region = raster::BBox{0, 0, 0, 0};
        return plan;
    }

    if (history.prev_static_scene_fingerprint != 0) {
        plan.scene_structure_unchanged =
            plan.frame_fingerprints.structure_fp ==
            history.prev_graph_structure_fingerprint;
        plan.static_camera_changed = camera_moved;
        plan.scene_is_static =
            sw_renderer->scene_hasher().is_static_scene_at(scene, frame);
    }

    // Fast path #2: static-scene reuse.
    const bool static_frame_eligible = has_surface &&
        (history.prev_frame == frame ||
         (plan.scene_is_static && history.prev_frame == frame - 1));
    const bool active_at_unchanged =
        plan.frame_fingerprints.active_at_fp != 0 &&
        plan.frame_fingerprints.active_at_fp ==
            history.prev_active_at_fingerprint;
    if (!has_projected_surface && static_frame_eligible &&
        plan.scene_structure_unchanged && !plan.static_camera_changed &&
        active_at_unchanged && history.prev_static_scene_fingerprint != 0 &&
        plan.frame_fingerprints.static_fp ==
            history.prev_static_scene_fingerprint) {
        record_surface_reuse(
            sw_renderer, frame, camera,
            plan.frame_fingerprints.combined_fp,
            width, height, ctx.policy.diagnostics_enabled,
            "static-fastpath");
        set_reuse_surface(
            plan, sw_renderer->buffer_ring().prev_framebuffer(),
            "static_scene_reuse");
        plan.dirty_region = raster::BBox{0, 0, 0, 0};
    }
    return plan;
}

FrameExecutionPlan ExecutionResolver::resolve(
    FrameExecutionPlan plan,
    const detail::LayerResolutionResult& resolved,
    const Scene& scene,
    const Camera2_5D& camera,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    int width,
    int height,
    const effects::EffectCatalog* effect_catalog,
    bool encode_requested,
    bool diagnostics_enabled) {
    (void)scene;
    (void)camera;
    plan.dirty_region = dirty_out.dirty_rect;
    plan.dirty_tiles = dirty_out.dirty_tiles;
    plan.encode = encode_requested;

    // An early reuse plan has already passed all fingerprint/camera checks.
    if (plan.path == FrameExecutionPath::ReuseSurface && plan.reuse_surface) {
        return plan;
    }

    // Fast path #3: the dirty collector proved that the previous surface is
    // already complete. This is now a resolver decision, not a scene.cpp
    // policy branch.
    if (sw_renderer && settings.dirty.enabled &&
        dirty_out.dirty_rect && dirty_out.dirty_rect->is_empty() &&
        has_matching_previous_surface(sw_renderer, width, height)) {
        if (diagnostics_enabled) {
            spdlog::info("[dirty-debug] frame={} surface_reuse=1",
                         static_cast<int>(frame));
        }
        sw_renderer->dirty_telemetry().last_dirty_area_ratio = 0.0;
        sw_renderer->update_dirty_telemetry(
            true, dirty_out.dirty_rect, false, true, false);
        set_reuse_surface(
            plan, sw_renderer->buffer_ring().prev_framebuffer(),
            "empty_dirty_region");
        return plan;
    }

    apply_sparse_or_full_decision(
        plan, resolved, settings, dirty_out, dirty_ratio, sw_renderer,
        frame, effect_catalog);
    return plan;
}

TileDecision ExecutionResolver::decide(
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    const SoftwareRenderer* sw_renderer,
    Frame frame,
    const effects::EffectCatalog* effect_catalog) {
    FrameExecutionPlan plan;
    apply_sparse_or_full_decision(
        plan, resolved, settings, dirty_out, dirty_ratio, sw_renderer,
        frame, effect_catalog);

    TileDecision result;
    result.enabled = plan.path == FrameExecutionPath::SparseTiles;
    result.path = plan.path;
    // Preserve the legacy adapter's no-dirty-tiles path token. The complete
    // plan intentionally remains FullRgb because no actual surface was passed.
    if (!result.enabled && plan.reason == "no_dirty_tiles") {
        result.path = FrameExecutionPath::ReuseSurface;
    }
    result.decode = plan.decode;
    result.composite = plan.composite;
    result.encode = plan.encode;
    result.reason_if_disabled = result.enabled ? std::string{} : plan.reason;
    return result;
}

} // namespace chronon3d::graph
