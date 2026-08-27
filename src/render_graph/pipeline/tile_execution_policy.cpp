#include "tile_execution_policy.hpp"

#include "camera_change_policy.hpp"
#include "scene_fingerprint.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

namespace chronon3d::graph {

bool ExecutionDecision::reuses_surface() const noexcept {
    return path == FrameExecutionPath::ReuseSurface;
}

bool ExecutionDecision::renders_full_rgb() const noexcept {
    return path == FrameExecutionPath::FullRgb;
}

bool ExecutionDecision::copies_gop() const noexcept {
    return path == FrameExecutionPath::CopyGop && copy_gop_plan.has_value();
}

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

bool has_frame_dependent_content(const Scene& scene, Frame frame) {
    for (const auto& transition : scene.clip_transitions()) {
        if (transition.duration > 0 &&
            frame >= transition.from &&
            frame <= transition.from + transition.duration) {
            return true;
        }
    }
    for (const auto& layer : scene.layers()) {
        if (!layer.active_at(frame)) continue;
        const auto has_transition = [](const LayerTransitionSpec& spec) {
            return !spec.transition_id.empty() && spec.transition_id != "none" &&
                   spec.duration > 0.0;
        };
        if (has_transition(layer.transition_in) || has_transition(layer.transition_out) ||
            layer.anim_transform.is_time_dependent() ||
            (layer.kind == LayerKind::Video && layer.video_source)) {
            return true;
        }
        for (const auto& node : layer.nodes) {
            if (node.shape.type() != ShapeType::TextRun) continue;
            const auto handle = node.shape.text_run_shape_handle();
            if (handle.value && !handle.value->animators.empty()) return true;
        }
    }
    return false;
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
    std::string_view reason,
    bool force_full_frame_clear = false) {
    plan.path = FrameExecutionPath::FullRgb;
    plan.decode = false;
    plan.render = true;
    plan.composite = true;
    plan.convert_to_rgb = false;
    plan.convert_to_yuv = false;
    plan.reason = reason;
    plan.use_dirty_region = false;
    plan.force_full_frame_clear = force_full_frame_clear;
    plan.output_surface.reset();
    plan.previous_surface.reset();
    plan.previous_framebuffer.reset();
    plan.copy_previous_surface = false;
    plan.dirty_regions.clear();
    plan.reuse_surface.reset();
    plan.copy_gop_plan.reset();
}

void set_reuse_surface(
    FrameExecutionPlan& plan,
    std::shared_ptr<Framebuffer> surface,
    std::string_view reason) {
    plan.path = FrameExecutionPath::ReuseSurface;
    plan.decode = false;
    plan.render = false;
    plan.composite = false;
    plan.convert_to_rgb = false;
    plan.convert_to_yuv = false;
    plan.output_surface.reset();
    plan.previous_surface.reset();
    plan.previous_framebuffer.reset();
    plan.copy_previous_surface = false;
    plan.dirty_regions.clear();
    plan.reuse_surface = std::move(surface);
    plan.use_dirty_region = false;
    plan.reason = reason;
    plan.copy_gop_plan.reset();
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
    if (dirty_ratio > settings.dirty.tile_dirty_ratio_threshold) {
        set_full_rgb(plan, "dirty_ratio_too_high");
        return;
    }

    // This is the sole sparse/full policy. The coordinator only executes the
    // path selected here.
    if (detail::has_layer_with_spatial_effects(resolved, frame, effect_catalog)) {
        // Predictable blur/glow spread is already expanded by
        // FrameDeltaCompiler. Only effects whose damage cannot be bounded
        // safely retain the full-frame fallback.
        bool unbounded_spatial_effect = false;
        for (const auto& resolved_layer : resolved.layers) {
            if (!resolved_layer.layer || !resolved_layer.layer->active_at(frame)) continue;
            bool layer_changed = true;
            if (dirty_out.frame_delta) {
                layer_changed = false;
                for (const auto& change : dirty_out.frame_delta->changes) {
                    if (std::string_view(change.instance_id) == std::string_view(resolved_layer.layer->name)) {
                        layer_changed = true;
                        break;
                    }
                }
            }
            if (layer_changed && !detail::is_safe_for_dirty_rects(
                    *resolved_layer.layer, false, effect_catalog)) {
                unbounded_spatial_effect = true;
                break;
            }
        }
        if (unbounded_spatial_effect) {
            set_full_rgb(plan, "unbounded_spatial_effect");
            return;
        }
    }
    if (!dirty_out.use_dirty_tiles) {
        set_full_rgb(plan, dirty_out.frame_delta && dirty_out.frame_delta->full_frame_dirty
            ? "full_frame_delta" : "dirty_rects_not_active");
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
    plan.previous_framebuffer = sw_renderer->buffer_ring().prev_framebuffer();
    plan.previous_surface.reset();
    plan.copy_previous_surface = true;

    const auto& cost = sw_renderer->dirty_telemetry();
    if (cost.tile_cost_model_ready() &&
        cost.tile_exec_ms_ewma > cost.full_frame_exec_ms_ewma * 1.10 &&
        dirty_ratio < 0.95) {
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

ExecutionDecision ExecutionResolver::resolve_initial(
    const detail::FrameDelta& delta) noexcept {
    if (!delta.scene_changed && !delta.camera_changed &&
        (delta.reuse.resolved_scene_reuse || delta.reuse.static_scene_reuse)) {
        return ExecutionDecision{
            FrameExecutionPath::ReuseSurface,
            "reuse_surface"};
    }

    if (delta.camera_changed) {
        return ExecutionDecision{FrameExecutionPath::FullRgb, "camera_changed"};
    }
    if (delta.scene_changed) {
        return ExecutionDecision{FrameExecutionPath::FullRgb, "scene_changed"};
    }
    if (!delta.reuse.reason.empty()) {
        return ExecutionDecision{FrameExecutionPath::FullRgb, delta.reuse.reason};
    }
    return ExecutionDecision{FrameExecutionPath::FullRgb, "reuse_not_eligible"};
}

ExecutionDecision ExecutionResolver::resolve_copy_gop(
    const detail::FrameDelta& delta,
    const CopyGopEligibility& eligibility) noexcept {
    if (eligibility.eligible() && !delta.scene_changed &&
        !delta.camera_changed && !delta.video_source_changed) {
        return ExecutionDecision{
            FrameExecutionPath::CopyGop,
            "copy_gop",
            eligibility.plan};
    }
    const auto reason = !eligibility.failure_reason.empty()
        ? eligibility.failure_reason
        : (delta.video_source_changed ? std::string_view{"video_source_changed"}
                                       : std::string_view{"copy_gop_not_eligible"});
    return ExecutionDecision{FrameExecutionPath::FullRgb, reason};
}

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

    // FrameDeltaCompiler is the sole authority for previous/current
    // fingerprint and camera comparisons.  This phase has scene-level state;
    // the resolved layer map is compiled later by the same authority through
    // compute_dirty_rect().
    plan.frame_fingerprints = compute_frame_fingerprints(
        sw_renderer->scene_hasher(), scene, frame,
        ctx.services.registry_generation);

    const bool has_surface = has_matching_previous_surface(sw_renderer, width, height);
    const bool previous_fingerprints_valid =
        history.prev_scene_fingerprint != 0 ||
        history.prev_static_scene_fingerprint != 0;
    const bool scene_is_static = previous_fingerprints_valid &&
        sw_renderer->scene_hasher().is_static_scene_at(scene, frame);

    detail::FrameStateSnapshot previous_state;
    previous_state.frame = history.prev_frame;
    previous_state.fingerprints = FrameFingerprints{
        history.prev_static_scene_fingerprint,
        history.prev_active_at_fingerprint,
        history.prev_graph_structure_fingerprint,
        history.prev_scene_fingerprint};
    previous_state.fingerprints_valid = previous_fingerprints_valid;
    previous_state.camera = history.prev_camera;
    previous_state.camera_valid = history.prev_camera_valid;

    detail::FrameStateSnapshot current_state;
    current_state.frame = frame;
    current_state.fingerprints = plan.frame_fingerprints;
    current_state.fingerprints_valid = true;
    current_state.camera = camera;
    current_state.camera_valid = camera.enabled;
    current_state.has_projected_surface = has_projected_surface;
    current_state.has_previous_surface = has_surface;
    current_state.scene_is_static = scene_is_static;
    // Layer maps are intentionally absent in this early phase. Dynamic
    // reuse must wait for compute_dirty_rect(), which supplies the complete
    // FrameDelta from resolved layer state.
    current_state.layer_state_complete = false;
    previous_state.layer_state_complete = false;

    const auto reuse = detail::FrameDeltaCompiler::compile_state(
        previous_state, current_state, width, height);

    if (previous_fingerprints_valid) {
        plan.scene_structure_unchanged = reuse.reuse.structure_unchanged;
        plan.static_camera_changed = !reuse.reuse.camera_unchanged;
        plan.scene_is_static = scene_is_static;
    }

    if (reuse.reuse.resolved_scene_reuse || reuse.reuse.static_scene_reuse) {
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

    if (reuse.reuse.static_scene_reuse && current_state.layer_state_complete) {
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
    bool diagnostics_enabled,
    runtime::PixelFormat output_format,
    const std::optional<CopyGopEligibility>& copy_gop) {
    (void)scene;
    (void)camera;
    plan.dirty_region = dirty_out.dirty_rect;
    plan.dirty_tiles = dirty_out.dirty_tiles;
    plan.encode = encode_requested;

    // CopyGop is evaluated before surface reuse because it replaces render and
    // encode work with packet muxing for a certified untouched segment.
    if (copy_gop && dirty_out.frame_delta) {
        const auto decision = ExecutionResolver::resolve_copy_gop(
            *dirty_out.frame_delta, *copy_gop);
        if (decision.copies_gop()) {
            plan.path = FrameExecutionPath::CopyGop;
            plan.decode = false;
            plan.render = false;
            plan.composite = false;
            plan.convert_to_rgb = false;
            plan.convert_to_yuv = false;
            plan.encode = true;
            plan.copy_gop_plan = decision.copy_gop_plan;
            plan.reason = decision.reason;
            return plan;
        }
    }

    // An early reuse plan has already passed all fingerprint/camera checks.
    if (plan.path == FrameExecutionPath::ReuseSurface && plan.reuse_surface) {
        return plan;
    }

    // Fast path #3: the dirty collector proved that the previous surface is
    // already complete. This is now a resolver decision, not a scene.cpp
    // policy branch.
    if (sw_renderer && settings.dirty.enabled && dirty_out.frame_delta_clean &&
        dirty_out.dirty_rect && dirty_out.dirty_rect->is_empty() &&
        !has_frame_dependent_content(scene, frame) &&
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

    const bool yuv_output = output_format == runtime::PixelFormat::Nv12 ||
                            output_format == runtime::PixelFormat::P010;
    if (yuv_output && dirty_out.frame_delta) {
        if (!dirty_out.frame_delta->scene_changed &&
            !dirty_out.frame_delta->camera_changed) {
            plan.path = FrameExecutionPath::FullYuv;
            plan.reason = output_format == runtime::PixelFormat::P010
                ? "clean_yuv_p010" : "clean_yuv_nv12";
            return plan;
        }
        if (dirty_out.use_dirty_tiles && dirty_out.tile_grid &&
            dirty_out.dirty_tiles && dirty_out.dirty_tiles->any()) {
            plan.path = FrameExecutionPath::SparseYuv;
            plan.reason = output_format == runtime::PixelFormat::P010
                ? "sparse_yuv_p010" : "sparse_yuv_nv12";
            plan.dirty_regions = ExecutionResolver::coalesce_dirty_regions(
                *dirty_out.tile_grid, *dirty_out.dirty_tiles);
            plan.use_dirty_region = !plan.dirty_regions.empty();
            if (plan.use_dirty_region) return plan;
        }
        plan.path = FrameExecutionPath::FullYuv;
        plan.reason = output_format == runtime::PixelFormat::P010
            ? "full_yuv_p010" : "full_yuv_nv12";
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
    result.reason_if_disabled = result.enabled ? std::string_view{} : plan.reason;
    return result;
}

} // namespace chronon3d::graph
