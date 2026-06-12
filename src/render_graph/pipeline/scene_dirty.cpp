// ---------------------------------------------------------------------------
// scene_dirty.cpp — Dirty region tracking entry point
//
// Helper functions (scroll optimization, parallel layer bboxes, scene root
// bboxes) have been extracted to scene_dirty_helpers.hpp.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
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
        return out;
    }

    // ── Tile-based dirty tracking setup ─────────────────────────────────
    const int effective_tile_size = settings.dirty.tile_size > 0 ? settings.dirty.tile_size : 256;
    const bool tiles_enabled = settings.dirty.tiles_active();
    raster::TileGrid tile_grid;
    raster::DirtyTileMask tile_mask;
    if (tiles_enabled) {
        tile_grid = raster::TileGrid(width, height, effective_tile_size);
        tile_mask = raster::DirtyTileMask(tile_grid);
    }

    // ── Diff current vs. previous layer bboxes ──────────────────────────
    {
        CHRONON_ZONE_C("dirty_rect_compute", trace_category::kFrame);

        const bool cam_changed = camera_changed(cam25d, &sw_renderer->frame_history().prev_camera, sw_renderer->frame_history().prev_camera_valid);

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
            if (tiles_enabled) {
                tile_mask.mark_bbox(tile_grid, clipped);
            }
        };

        auto same_bbox = [](const raster::BBox& a, const raster::BBox& b) -> bool {
            return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
        };

        auto add_layer_dirty = [&](const LayerBBoxState& curr,
                                   const LayerBBoxState* prev) {
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
                (cam_changed && curr.uses_2_5d_projection) ||
                (curr.world_matrix != prev->world_matrix);
            const bool content_changed =
                !curr.cache_static ||
                curr.opacity != prev->opacity ||
                curr.content_hash != prev->content_hash;

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
            auto prev_it = sw_renderer->layer_history().prev_layer_bboxes.find(pair.first);
            add_layer_dirty(
                curr,
                prev_it == sw_renderer->layer_history().prev_layer_bboxes.end() ? nullptr : &prev_it->second
            );
        }

        // Layers removed since previous frame
        for (const auto& pair : sw_renderer->layer_history().prev_layer_bboxes) {
            if (out.layer_bboxes.find(pair.first) == out.layer_bboxes.end()) {
                add_dirty_bbox(pair.second.bbox);
            }
        }

        // ── Try scroll optimisation ─────────────────────────────────────
        auto scroll_rect = try_scroll_optimization(sw_renderer, cam25d, width, height);
        if (scroll_rect.has_value()) {
            out.dirty_rect = *scroll_rect;
            if (tiles_enabled) {
                tile_mask.mark_bbox(tile_grid, *scroll_rect);
            }
        } else {
            out.dirty_rect = has_dirty ? std::optional(union_dirty)
                                       : std::optional(raster::BBox{0, 0, 0, 0});

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

    return out;
}

} // namespace chronon3d::graph::detail
