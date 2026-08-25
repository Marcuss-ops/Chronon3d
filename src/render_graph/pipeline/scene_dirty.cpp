// ---------------------------------------------------------------------------
// scene_dirty.cpp — Dirty region tracking entry point
//
// Helper functions (scroll optimization, parallel layer bboxes, scene root
// bboxes) have been extracted to scene_dirty_helpers.hpp.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
// WP-3 PR 3.2 — `<chronon3d/math/renderer_state.hpp>` is now a thin
// shim that forwards canonical includes; no direct dependency needed
// from this file.  `LayerBBoxState` resolves through `software_renderer.hpp`
// (transitively to `<chronon3d/runtime/dirty_history.hpp>`).
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "scene_dirty_helpers.hpp"
#include "scene_internal.hpp"
#include <algorithm>

namespace chronon3d::graph::detail {

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

    const auto t_dirty0 = profiling::now();

    const Camera2_5DRuntime& cam25d = resolved.camera.camera;

    // ── Parallel layer bbox computation ─────────────────────────────────
    out.layer_bboxes = compute_layer_bboxes_parallel(
        ctx, resolved, cam25d, sw_renderer, settings, width, height, frame);

    // Include scene root nodes in dirty-rect tracking
    compute_scene_root_bboxes(out.layer_bboxes, scene, ctx, sw_renderer);

    // ── Decide whether to use dirty rects ───────────────────────────────
    out.use_dirty_rects = settings.dirty.enabled &&
                          sw_renderer->buffer_ring().prev_framebuffer() &&
                          sw_renderer->buffer_ring().prev_framebuffer()->width() == width &&
                          sw_renderer->buffer_ring().prev_framebuffer()->height() == height &&
                          sw_renderer->frame_history().prev_frame == frame - 1;

    if (!out.use_dirty_rects) {
        out.dirty_rect = raster::BBox{0, 0, width, height};
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->dirty_eval_wall_ms.fetch_add(
                static_cast<uint64_t>(profiling::duration_ms(t_dirty0, profiling::now())),
                std::memory_order_relaxed);
        }
        return out;
    }

    // ── Tile-based dirty tracking setup ─────────────────────────────────
    const bool has_projected_25d_layer = std::any_of(
        resolved.layers.begin(), resolved.layers.end(),
        [frame](const ResolvedLayer& layer) {
            return layer.layer && layer.layer->active_at(frame) &&
                   layer.layer->uses_2_5d_projection;
        });
    const bool cam_changed = FrameDeltaCompiler::camera_unchanged(
        cam25d, &sw_renderer->frame_history().prev_camera,
        sw_renderer->frame_history().prev_camera_valid) == false;
    if (has_projected_25d_layer) {
        out.dirty_rect = raster::BBox{0, 0, width, height};
        out.use_dirty_rects = false;
        return out;
    }

    const int effective_tile_size = settings.dirty.tile_size > 0 ? settings.dirty.tile_size : 256;
    const bool tiles_enabled = settings.dirty.tiles_active();
    raster::TileGrid tile_grid;
    raster::DirtyTileMask tile_mask;
    if (tiles_enabled) {
        tile_grid = raster::TileGrid(width, height, effective_tile_size);
        tile_mask = raster::DirtyTileMask(tile_grid);
    }

    // ── Compile current-vs-previous layer delta ────────────────────────
    // FrameDeltaCompiler is the single owner of old/new bounds unioning and
    // tile marking.  Scroll optimization and overflow policy remain explicit
    // policies around this pure delta compilation step.
    FrameDelta delta;
    {
        CHRONON_TRACE_SCOPE("chronon.frame", "dirty_rect_compute");
        delta = FrameDeltaCompiler::compile(
            frame,
            out.layer_bboxes,
            sw_renderer->dirty_telemetry().previous_layers,
            cam_changed,
            width,
            height,
            tiles_enabled ? &tile_grid : nullptr);
        out.frame_delta = delta;
        out.dirty_rect = delta.dirty_bounds;
        if (delta.dirty_tiles) {
            tile_mask = std::move(*delta.dirty_tiles);
        }

        // ── Try scroll optimisation ─────────────────────────────────────
        // A framebuffer shift is only valid when every moving layer shares
        // the same screen-space translation.  Projected 2.5D layers do not:
        // camera X/Y motion produces a different parallax displacement at
        // each depth.  Shifting the previous composite in that case leaves
        // some TextRun surfaces stale until the next full redraw, which
        // presents as intermittent missing/glitched words.
        const bool has_projected_25d_layer = std::any_of(
            resolved.layers.begin(), resolved.layers.end(),
            [frame](const ResolvedLayer& layer) {
                return layer.layer && layer.layer->active_at(frame) &&
                       (layer.layer->uses_2_5d_projection ||
                        layer.layer->is_native_3d());
            });
        auto scroll_rect = has_projected_25d_layer
            ? std::optional<raster::BBox>{}
            : try_scroll_optimization(sw_renderer, cam25d, width, height);
        if (scroll_rect.has_value()) {
            out.dirty_rect = *scroll_rect;
            if (tiles_enabled) {
                tile_mask.mark_bbox(tile_grid, *scroll_rect);
            }
        } else {
            // ── Dirty rect overflow protection ─────────────────────
            // When the dirty union exceeds 50% of the frame, reset to
            // full-frame to avoid pathological expansion (105%+ overlap).
            // Continuous animations cause progressive union growth;
            // this threshold ensures we don't spend more time computing
            // dirty rects than we save from rendering fewer pixels.
            if (out.dirty_rect && !out.dirty_rect->is_empty()) {
                const int dw = out.dirty_rect->x1 - out.dirty_rect->x0;
                const int dh = out.dirty_rect->y1 - out.dirty_rect->y0;
                const int64_t dirty_area = static_cast<int64_t>(dw) * dh;
                const int64_t frame_area = static_cast<int64_t>(width) * height;
                const int64_t half_frame = frame_area / 2;
                if (dirty_area > half_frame) {
                    out.dirty_rect = raster::BBox{0, 0, width, height};
                }
            }
        }
    }

    if (out.use_dirty_rects && out.dirty_rect) {
        if (out.dirty_rect->x0 <= 0 && out.dirty_rect->y0 <= 0 &&
            out.dirty_rect->x1 >= width && out.dirty_rect->y1 >= height) {
            out.use_dirty_rects = false;
        }
    }

    // ── Populate tile-based dirty output ────────────────────────────────
    if (tiles_enabled) {
        out.tile_grid = std::move(tile_grid);
        out.dirty_tiles = std::move(tile_mask);
        out.use_dirty_tiles = out.use_dirty_rects && out.dirty_tiles->any();

        if (!out.dirty_tiles->any()) {
            out.use_dirty_tiles = false;
            out.dirty_rect = raster::BBox{0, 0, 0, 0};
        }
    }

    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->dirty_eval_wall_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(t_dirty0, profiling::now())),
            std::memory_order_relaxed);
    }
    return out;
}

} // namespace chronon3d::graph::detail
