#pragma once

// ---------------------------------------------------------------------------
// scene_dirty_helpers.hpp
//
// Internal helpers for dirty region tracking.
// Header-only (static inline) — extracted from scene_dirty.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include "scene_internal.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace chronon3d::graph::detail {

// ── Scrolling shortcut ───────────────────────────────────────────────────────
// When only the camera position has changed by an integer amount we can shift
// the previous framebuffer and only re-render the newly exposed strip.
[[nodiscard]] static inline std::optional<raster::BBox> try_scroll_optimization(
    SoftwareRenderer* sw_renderer,
    const Camera2_5D& cam25d,
    i32 width,
    i32 height
) {
    if (!sw_renderer->m_prev_camera_valid || !sw_renderer->m_prev_camera.enabled || !cam25d.enabled) {
        return std::nullopt;
    }

    const Vec3& cp = cam25d.position;
    const Vec3& pp = sw_renderer->m_prev_camera.position;
    Vec3 camera_delta = cp - pp;

    const bool camera_params_compatible =
        std::abs(sw_renderer->m_prev_camera.zoom - cam25d.zoom) < 1e-3f;

    if (!camera_params_compatible || std::abs(camera_delta.z) >= 1e-3f) {
        return std::nullopt;
    }

    const i32 dx = -static_cast<i32>(std::round(camera_delta.x));
    const i32 dy = -static_cast<i32>(std::round(camera_delta.y));

    const bool scroll_eligible =
        std::abs(camera_delta.x - std::round(camera_delta.x)) < 0.1f &&
        std::abs(camera_delta.y - std::round(camera_delta.y)) < 0.1f &&
        (dx != 0 || dy != 0) &&
        std::abs(dx) < width &&
        std::abs(dy) < height;

    if (!scroll_eligible) return std::nullopt;

    CHRONON_ZONE_C("scroll_optimization", trace_category::kFrame);

    if (sw_renderer->m_prev_framebuffer.use_count() > 1) {
        sw_renderer->m_prev_framebuffer = std::make_shared<Framebuffer>(*sw_renderer->m_prev_framebuffer);
    }
    sw_renderer->m_prev_framebuffer->shift(dx, dy);

    raster::BBox strip{0, 0, width, height};
    if (dx > 0) strip.x1 = dx;
    else if (dx < 0) strip.x0 = width + dx;
    if (dy > 0) strip.y1 = dy;
    else if (dy < 0) strip.y0 = height + dy;
    return strip;
}

// ── Parallel layer bbox computation ──────────────────────────────────────────
// Computes bboxes for all active resolved layers using TBB parallelism.
[[nodiscard]] static inline std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> compute_layer_bboxes_parallel(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Camera2_5DRuntime& cam25d,
    SoftwareRenderer* sw_renderer,
    const RenderSettings& settings,
    i32 width,
    i32 height,
    Frame frame
) {
    auto compute_bbox_for_resolved = [&](const ResolvedLayer& rl) -> raster::BBox {
        LayerGraphItem item;
        if (cam25d.enabled && rl.layer->uses_2_5d_projection) {
            Transform effective_transform = rl.world_transform;
            auto proj = project_layer_2_5d(
                effective_transform, effective_transform.to_mat4(), cam25d,
                static_cast<f32>(width), static_cast<f32>(height), ctx.options.diagnostics_enabled);
            if (proj.visible) {
                const Mat4 eff_proj = detail::is_native_3d_layer(*rl.layer) ? Mat4(1.0f) : proj.projection_matrix;
                item = LayerGraphItem{
                    .layer = rl.layer,
                    .transform = proj.transform,
                    .world_matrix = rl.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth = proj.depth,
                    .world_z = rl.world_transform.position.z,
                    .projected = true,
                    .native_3d = detail::is_native_3d_layer(*rl.layer),
                    .insertion_index = rl.insertion_index
                };
            } else {
                return raster::BBox{0, 0, 0, 0};
            }
        } else {
            item = LayerGraphItem{
                .layer = rl.layer,
                .transform = rl.world_transform,
                .world_matrix = rl.world_matrix,
                .depth = 0.0f,
                .world_z = rl.world_transform.position.z,
                .projected = false,
                .native_3d = detail::is_native_3d_layer(*rl.layer),
                .insertion_index = rl.insertion_index
            };
        }
        return detail::compute_layer_bbox(item, ctx, sw_renderer);
    };

    tbb::enumerable_thread_specific<std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState>> tls_bboxes;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, resolved.layers.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            auto& local_map = tls_bboxes.local();
            for (size_t i = r.begin(); i < r.end(); ++i) {
                const auto& rl = resolved.layers[i];
                if (rl.layer && rl.layer->active_at(frame)) {
                    raster::BBox bbox = compute_bbox_for_resolved(rl);

                    if (!is_safe_for_dirty_rects(*rl.layer, settings.motion_blur.enabled)) {
                        bbox = raster::BBox{0, 0, width, height};
                        if (ctx.telemetry.counters) {
                            ctx.telemetry.counters->increment_dirty_full_fallback_reason(
                                DirtyFallbackReason::EffectBoundsUnknown);
                        }
                    }

                    SoftwareRenderer::LayerBBoxState state;
                    state.bbox = bbox;
                    state.world_matrix = rl.world_matrix;
                    state.opacity = rl.world_transform.opacity;
                    state.visible = rl.layer->visible;
                    state.cache_static = rl.layer->cache_static;
                    state.uses_2_5d_projection = rl.layer->uses_2_5d_projection;
                    state.content_hash = rl.layer->get_static_hash();
                    local_map[std::string(rl.layer->name)] = state;
                }
            }
        }
    );

    std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> merged;
    for (auto& local_map : tls_bboxes) {
        for (auto&& [name, state] : local_map) {
            merged[name] = std::move(state);
        }
    }
    return merged;
}

// ── Scene root node bboxes ───────────────────────────────────────────────────
// Computes bboxes for scene root nodes (non-layer sources).
static inline void compute_scene_root_bboxes(
    std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState>& bboxes,
    const Scene& scene,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer
) {
    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
        const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame.frame.width * 0.5f, ctx.frame.frame.height * 0.5f, 0.0f));
        Mat4 matrix;
        if (ctx.options.modular_coordinates) {
            matrix = canvas_center * ssaa_scale * node.world_transform.to_mat4();
        } else {
            matrix = ssaa_scale * node.world_transform.to_mat4();
        }
        auto* processor = sw_renderer->software_registry().get_shape(node.shape.type);
        if (!processor) continue;
        f32 spread = 0.0f;
        if (node.shadow.enabled) spread = std::max(spread, node.shadow.radius);
        if (node.glow.enabled)   spread = std::max(spread, node.glow.radius);
        raster::BBox bbox = processor->compute_world_bbox(node.shape, matrix, spread);

        SoftwareRenderer::LayerBBoxState state;
        state.bbox = bbox;
        state.world_matrix = node.world_transform.to_mat4();
        state.opacity = node.world_transform.opacity;
        state.visible = node.visible;
        state.cache_static = true;
        state.uses_2_5d_projection = false;
        state.content_hash = hash_render_node(node);
        bboxes["root.node:" + std::string(node.name)] = state;
    }
}

} // namespace chronon3d::graph::detail
