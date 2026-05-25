#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include "builder/graph_builder_internal.hpp"
#include "builder/graph_builder_pipeline.hpp"
#include "render_pipeline_scene_internal.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <algorithm>

namespace chronon3d::graph::detail {

// ── Scrolling shortcut ───────────────────────────────────────────────────────
// When only the camera position has changed by an integer amount we can shift
// the previous framebuffer and only re-render the newly exposed strip.
static std::optional<raster::BBox> try_scroll_optimization(
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

// ── Dirty rect main entry point ──────────────────────────────────────────────

DirtyRectOutput compute_dirty_rect(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Scene& scene,
    const RenderSettings& settings,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    i32 width,
    i32 height
) {
    DirtyRectOutput out;

    if (!sw_renderer) {
        out.dirty_rect = raster::BBox{0, 0, width, height};
        return out;
    }

    const Camera2_5DRuntime& cam25d = resolved.camera.camera;

    // ── Helper: compute single-layer bbox ───────────────────────────────
    auto compute_bbox_for_resolved = [&](const ResolvedLayer& rl, const Camera2_5DRuntime& cam) -> raster::BBox {
        LayerGraphItem item;
        if (cam.enabled && rl.layer->is_3d) {
            Transform effective_transform = rl.world_transform;
            auto proj = project_layer_2_5d(
                effective_transform, effective_transform.to_mat4(), cam,
                static_cast<f32>(width), static_cast<f32>(height), ctx.diagnostics_enabled);
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

    // ── Parallel layer bbox computation ─────────────────────────────────
    tbb::enumerable_thread_specific<std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState>> tls_bboxes;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, resolved.layers.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            auto& local_map = tls_bboxes.local();
            for (size_t i = r.begin(); i < r.end(); ++i) {
                const auto& rl = resolved.layers[i];
                if (rl.layer && rl.layer->active_at(frame)) {
                    raster::BBox bbox = compute_bbox_for_resolved(rl, cam25d);

                    // ── Check whether this layer is safe for dirty rects ─
                    // Effects like blur, non-Normal blend modes, and active masks
                    // bleed outside the geometric bbox — force full-frame for safety.
                    if (!is_safe_for_dirty_rects(*rl.layer, settings.motion_blur.enabled)) {
                        bbox = raster::BBox{0, 0, width, height};
                        if (ctx.counters) {
                            ctx.counters->increment_dirty_full_fallback_reason(
                                DirtyFallbackReason::EffectBoundsUnknown);
                        }
                    }

                    SoftwareRenderer::LayerBBoxState state;
                    state.bbox = bbox;
                    state.world_matrix = rl.world_matrix;
                    state.opacity = rl.world_transform.opacity;
                    state.visible = rl.layer->visible;
                    state.cache_static = rl.layer->cache_static;
                    state.is_3d = rl.layer->is_3d;
                    local_map[std::string(rl.layer->name)] = state;
                }
            }
        }
    );

    // Merge TLS results
    {
        std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> merged;
        for (auto& local_map : tls_bboxes) {
            for (auto&& [name, state] : local_map) {
                merged[name] = std::move(state);
            }
        }
        out.layer_bboxes = std::move(merged);
    }

    // Include scene root nodes in dirty-rect tracking
    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
        const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        Mat4 matrix;
        if (ctx.modular_coordinates) {
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
        state.is_3d = false;
        out.layer_bboxes["root.node:" + std::string(node.name)] = state;
    }

    // ── Decide whether to use dirty rects ───────────────────────────────
    out.use_dirty_rects = settings.enable_dirty_rects &&
                          sw_renderer->m_prev_framebuffer &&
                          sw_renderer->m_prev_framebuffer->width() == width &&
                          sw_renderer->m_prev_framebuffer->height() == height &&
                          sw_renderer->m_prev_frame == frame - 1;

    if (!out.use_dirty_rects) {
        out.dirty_rect = raster::BBox{0, 0, width, height};
        return out;
    }

    // ── Diff current vs. previous layer bboxes ──────────────────────────
    {
        CHRONON_ZONE_C("dirty_rect_compute", trace_category::kFrame);

        const bool cam_changed = camera_changed(cam25d, &sw_renderer->m_prev_camera, sw_renderer->m_prev_camera_valid);

        raster::BBox union_dirty{0, 0, 0, 0};
        bool has_dirty = false;

        auto add_dirty_bbox = [&](const raster::BBox& b) {
            if (b.is_empty()) return;
            raster::BBox clipped = b;
            clipped.clip_to(width, height);
            if (clipped.is_empty()) return;
            if (!has_dirty) {
                union_dirty = clipped;
                has_dirty = true;
            } else {
                union_dirty.x0 = std::min(union_dirty.x0, clipped.x0);
                union_dirty.y0 = std::min(union_dirty.y0, clipped.y0);
                union_dirty.x1 = std::max(union_dirty.x1, clipped.x1);
                union_dirty.y1 = std::max(union_dirty.y1, clipped.y1);
            }
        };

        auto same_bbox = [](const raster::BBox& a, const raster::BBox& b) -> bool {
            return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
        };

        auto add_layer_dirty = [&](const SoftwareRenderer::LayerBBoxState& curr,
                                   const SoftwareRenderer::LayerBBoxState* prev) {
            const bool prev_exists = prev != nullptr;
            const bool curr_visible = curr.visible;
            const bool prev_visible = prev_exists ? prev->visible : false;

            if (!prev_exists) {
                add_dirty_bbox(curr.bbox);
                return;
            }

            if (curr_visible != prev_visible) {
                add_dirty_bbox(curr_visible ? curr.bbox : prev->bbox);
                return;
            }

            if (!curr_visible) return;

            const bool geometry_changed =
                (cam_changed && curr.is_3d) ||
                (curr.world_matrix != prev->world_matrix);
            const bool content_changed =
                !curr.cache_static ||
                curr.opacity != prev->opacity;

            if (geometry_changed) {
                if (same_bbox(curr.bbox, prev->bbox)) {
                    add_dirty_bbox(curr.bbox);
                } else {
                    add_dirty_bbox(curr.bbox);
                    add_dirty_bbox(prev->bbox);
                }
                return;
            }

            if (content_changed) {
                add_dirty_bbox(curr.bbox);
            }
        };

        // Diff current against previous
        for (const auto& pair : out.layer_bboxes) {
            const auto& curr = pair.second;
            auto prev_it = sw_renderer->m_prev_layer_bboxes.find(pair.first);
            add_layer_dirty(
                curr,
                prev_it == sw_renderer->m_prev_layer_bboxes.end() ? nullptr : &prev_it->second
            );
        }

        // Layers removed since previous frame
        for (const auto& pair : sw_renderer->m_prev_layer_bboxes) {
            if (out.layer_bboxes.find(pair.first) == out.layer_bboxes.end()) {
                add_dirty_bbox(pair.second.bbox);
            }
        }

        // ── Try scroll optimisation ─────────────────────────────────────
        auto scroll_rect = try_scroll_optimization(sw_renderer, cam25d, width, height);
        if (scroll_rect.has_value()) {
            out.dirty_rect = *scroll_rect;
        } else {
            out.dirty_rect = has_dirty ? std::optional(union_dirty)
                                       : std::optional(raster::BBox{0, 0, 0, 0});
        }
    }

    return out;
}

} // namespace chronon3d::graph::detail
