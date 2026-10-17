#include "tile_execution_policy.hpp"

#include "camera_change_policy.hpp"
#include "helpers.hpp"
#include "../core/scene_fingerprint.hpp"
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/effects/effect_catalog_data.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <set>
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
    // Sparse tiles preserve the previous frame by copying its CPU pixels
    // before rendering dirty regions. Native-video frames intentionally have
    // no CPU backing, so treating their device-only surface as a matching
    // previous framebuffer would make the coordinator dereference a null
    // pixel pointer (and would mix a CPU tile path with the GPU contract).
    return previous->width() == width && previous->height() == height &&
           previous->data() != nullptr;
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
        const bool has_mesh_source = std::any_of(
            layer.nodes.begin(), layer.nodes.end(), [](const RenderNode& node) {
                return node.shape.type() == ShapeType::Mesh &&
                    static_cast<bool>(node.shape.mesh_shape().inline_mesh);
            });
        if (has_transition(layer.transition_in) || has_transition(layer.transition_out) ||
            layer.anim_transform.is_time_dependent() ||
            (has_mesh_source && layer.is_native_3d()) ||
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

    // Canonical tile-prune authority: this is the only sparse/full policy.
    // FrameDeltaCompiler owns calculation of bounds/mask; the coordinator only
    // executes the immutable result and node_runner only applies per-node clip.
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
    if (!sw_renderer || !sw_renderer->execution_context().runtime) {
        set_full_rgb(plan, "missing_renderer_runtime");
        return;
    }
    if (!dirty_out.tile_grid || !dirty_out.dirty_tiles ||
        !dirty_out.dirty_tiles->any()) {
        set_full_rgb(plan, "no_dirty_tiles");
        return;
    }
    if (!settings.require_native_gpu &&
        !has_matching_previous_surface(sw_renderer, dirty_out.tile_grid->width(),
                                       dirty_out.tile_grid->height())) {
        set_full_rgb(plan, "missing_previous_surface");
        return;
    }

    // Coalescing is part of the canonical resolver decision and is performed
    // exactly once before the coordinator receives the plan.
    plan.dirty_regions = ExecutionResolver::coalesce_dirty_regions(
        *dirty_out.tile_grid, *dirty_out.dirty_tiles);
    if (plan.dirty_regions.empty()) {
        set_full_rgb(plan, "no_dirty_regions");
        return;
    }
    plan.previous_framebuffer = settings.require_native_gpu
        ? nullptr : sw_renderer->buffer_ring().prev_framebuffer();
    plan.previous_surface.reset();
    // Native sparse rendering preserves the completed device surface in the
    // current encode slot before graph execution.  The CPU coordinator must
    // not try to dereference a missing previous framebuffer on this path.
    plan.copy_previous_surface = !settings.require_native_gpu;

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

bool ExecutionResolver::native_clean_reuse_eligible(
    const Scene& scene,
    Frame frame,
    std::uint64_t registry_generation,
    const compiler::FrameParameterBlock* frame_parameters,
    const SoftwareRenderer* sw_renderer,
    bool has_previous_native_surface,
    std::string& reason_out) {
    const auto reject = [&reason_out](std::string_view reason) {
        reason_out = std::string(reason);
        return false;
    };
    if (!sw_renderer) return reject("missing_renderer_runtime");
    if (!has_previous_native_surface) return reject("missing_previous_native_surface");

    // Scene-level fingerprints + camera snapshots: same authority as
    // resolve_early_reuse, but without its telemetry/surface side effects.
    // The camera resolves here with resolve_scene_camera, the same authority
    // the render path wires into frame_input (scene_context_setup).
    const Camera2_5D camera = resolve_scene_camera(scene).camera;
    const auto& history = sw_renderer->frame_history();
    FrameFingerprints fingerprints =
        compute_frame_fingerprints(scene, frame, registry_generation);
    const bool typed_frame_dependent = frame_parameters &&
        std::any_of(frame_parameters->layers.begin(),
                    frame_parameters->layers.end(),
                    [](const compiler::SampledLayerParameters& layer) {
                        return layer.active && layer.dirty;
                    });
    if (typed_frame_dependent) {
        fingerprints.combined_fp ^=
            (static_cast<std::uint64_t>(frame.integral()) + 1ULL) *
            0x9e3779b97f4a7c15ULL;
    }
    const bool previous_fingerprints_valid =
        history.prev_scene_fingerprint != 0 ||
        history.prev_static_scene_fingerprint != 0;
    if (!previous_fingerprints_valid) return reject("fingerprints_unavailable");
    // Transition/animation/video/text-animator state is sampled by frame and
    // invisible to fingerprints. Report the descriptive reason before the
    // generic non-static one: an animated scene is rejected either way, but
    // the rejection must say WHY (frame-dependent content), not just that the
    // scene was not static — the zero-copy gates diff on this reason.
    if (has_frame_dependent_content(scene, frame)) {
        return reject("frame_dependent_content");
    }
    const bool scene_static = !typed_frame_dependent &&
        scene_is_static_at(scene, frame);
    if (!scene_static) return reject("not_static_scene");

    detail::FrameStateSnapshot previous_state;
    previous_state.frame = history.prev_frame;
    previous_state.fingerprints = FrameFingerprints{
        history.prev_static_scene_fingerprint,
        history.prev_active_at_fingerprint,
        history.prev_graph_structure_fingerprint,
        history.prev_scene_fingerprint};
    previous_state.fingerprints_valid = true;
    previous_state.camera = history.prev_camera;
    previous_state.camera_valid = history.prev_camera_valid;

    detail::FrameStateSnapshot current_state;
    current_state.frame = frame;
    current_state.fingerprints = fingerprints;
    current_state.fingerprints_valid = true;
    current_state.camera = camera;
    current_state.camera_valid = camera.enabled;
    current_state.has_projected_surface = std::any_of(
        scene.layers().begin(), scene.layers().end(),
        [frame](const Layer& layer) {
            return layer.active_at(frame) &&
                   (layer.uses_2_5d_projection || layer.is_native_3d());
        });
    if (current_state.has_projected_surface) return reject("projected_surface");
    current_state.has_previous_surface = true;
    current_state.scene_is_static = true;
    current_state.layer_state_complete = false;
    previous_state.layer_state_complete = false;

    const auto reuse = detail::FrameDeltaCompiler::compile_state(
        previous_state, current_state, 1, 1);
    if (!reuse.reuse.resolved_scene_reuse && !reuse.reuse.static_scene_reuse) {
        return reject(reuse.reuse.reason.empty() ? "no_reuse"
                                                 : reuse.reuse.reason);
    }

    // Transition/animation/video/text-animator state is sampled by frame and
    // invisible to fingerprints: same guard as the CPU surface-reuse path.
    if (has_frame_dependent_content(scene, frame)) {
        return reject("frame_dependent_content");
    }

    // Time-evolving effects with static params (grain speed, noise evolution)
    // hash identically every frame but emit different pixels. The catalog
    // temporal flag is the authority; any active temporal instance vetoes.
    static const auto temporal_types = [] {
        std::set<effects::EffectType> types;
        for (const auto& entry : effects::builtin_effect_catalog()) {
            if (entry.temporal) types.insert(entry.type);
        }
        return types;
    }();
    for (const auto& layer : scene.layers()) {
        if (!layer.active_at(frame)) continue;
        for (const auto& inst : layer.effects()) {
            if (!inst.enabled) continue;
            if (temporal_types.count(inst.effect_type) != 0) {
                return reject("active_temporal_effect");
            }
        }
    }

    reason_out = "native_clean_reuse";
    return true;
}

FrameExecutionPlan ExecutionResolver::resolve_early_reuse(    const RenderGraphContext& ctx,
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
        scene, frame, ctx.registry_generation);
    const bool typed_frame_dependent = ctx.frame_parameters &&
        std::any_of(ctx.frame_parameters->layers.begin(),
                    ctx.frame_parameters->layers.end(),
                    [](const compiler::SampledLayerParameters& layer) {
                        return layer.active && layer.dirty;
                    });
    if (typed_frame_dependent) {
        // Typed payloads (including baked mesh positions) are sampled outside
        // the immutable Scene fingerprint. Include frame identity in the pixel
        // reuse key so a scene-static fingerprint cannot reuse prior pixels.
        plan.frame_fingerprints.combined_fp ^=
            (static_cast<std::uint64_t>(frame.integral()) + 1ULL) *
            0x9e3779b97f4a7c15ULL;
    }

    const bool has_surface = has_matching_previous_surface(sw_renderer, width, height);
    const bool previous_fingerprints_valid =
        history.prev_scene_fingerprint != 0 ||
        history.prev_static_scene_fingerprint != 0;
    const bool scene_is_static = previous_fingerprints_valid &&
        !typed_frame_dependent && scene_is_static_at(scene, frame);

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
        // Native NVENC needs a framebuffer with a valid GPU backing for every
        // submitted frame. The reuse candidate is a host-owned historical
        // framebuffer and may not have a registered native surface, so
        // carrying it into the strict path causes a late residency failure
        // (often after several successful frames). Rebuild the frame graph
        // instead of silently attempting a CPU upload that the strict
        // contract forbids.
        if (settings.require_native_gpu) {
            set_full_rgb(plan, "native_gpu_disables_surface_reuse");
        } else {
            return plan;
        }
    }

    // Empty-damage reuse is also resolved here; coordinators never infer it.
    if (sw_renderer && !settings.require_native_gpu && settings.dirty.enabled && dirty_out.frame_delta_clean &&
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
            plan.dirty_regions = ExecutionResolver::coalesce_dirty_regions(
                *dirty_out.tile_grid, *dirty_out.dirty_tiles);
            // SparseYuv is represented in the plan taxonomy, but the scene
            // executor does not yet apply these regions to a persistent YUV
            // output surface. Do not report a sparse path while executing the
            // full graph; keep the output correct and make the missing executor
            // explicit in telemetry.
            if (!plan.dirty_regions.empty()) {
                plan.path = FrameExecutionPath::FullYuv;
                plan.reason = "sparse_yuv_executor_unavailable";
                return plan;
            }
        }
        plan.path = FrameExecutionPath::FullYuv;
        plan.reason = output_format == runtime::PixelFormat::P010
            ? "full_yuv_p010" : "full_yuv_nv12";
        return plan;
    }

    // SparseTiles is a CPU-preserve/copy path: it starts from the previous
    // framebuffer and patches dirty regions. A strict native-video frame is
    // device-only and is consumed asynchronously by CUDA/NVENC, so there is
    // no valid CPU surface to preserve and no safe partial-frame handoff.
    // Render the complete frame through the native graph instead. This keeps
    // the GPU contract strict and avoids both a readback and a null-pixel
    // copy; the Vulkan compositor still limits work internally where it can.
    if (encode_requested && settings.require_native_gpu) {
        set_full_rgb(plan, "native_gpu_disables_sparse_tiles");
        return plan;
    }

    apply_sparse_or_full_decision(
        plan, resolved, settings, dirty_out, dirty_ratio, sw_renderer,
        frame, effect_catalog);
    if (settings.require_native_gpu && plan.path == FrameExecutionPath::ReuseSurface) {
        set_full_rgb(plan, "native_gpu_disables_surface_reuse");
    }
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
