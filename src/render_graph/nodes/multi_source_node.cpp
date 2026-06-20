#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    bool centered, bool uses_2_5d_projection, RenderNodeCachePolicy policy
) : m_name(std::move(name)), m_items(std::move(items)), m_key(key),
    m_centered(centered), m_uses_2_5d_projection(uses_2_5d_projection), m_cache_policy(policy) {}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    for (const auto& item : m_items) {
        if (!item.node) continue;
        Mat4 matrix;
        if (m_uses_2_5d_projection || m_centered) {
            matrix = canvas_center * ssaa_scale * item.matrix;
        } else {
            matrix = ssaa_scale * item.matrix;
        }

        f32 spread = 0.0f;
        if (item.node->shadow.enabled) {
            spread = std::max(spread, item.node->shadow.radius + std::max(std::abs(item.node->shadow.offset.x), std::abs(item.node->shadow.offset.y)));
        }
        if (item.node->glow.enabled) {
            spread = std::max(spread, item.node->glow.radius);
        }
        spread += 8.0f;

        raster::BBox bbox;
        if (item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            // text_run items use the 2.5D-aware
            // `compute_text_run_world_bbox` instead of the regular
            // shape-driven `compute_world_bbox`.  The helper accounts for
            // per-glyph rotation.x/y shears and scale.z expansion,
            // per-glyph blur/stroke, and the shadow stack inside the
            // shape itself — the spread arg covers node-level shadow/glow.
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value, matrix, spread);
        } else if (ctx.camera.has_camera_2_5d &&
            (item.node->shape.type() == ShapeType::FakeBox3D || item.node->shape.type() == ShapeType::GridPlane)) {
            if (auto proj_bbox = detail::projected_native_3d_bbox(ctx, *item.node, item.matrix, spread)) {
                bbox = *proj_bbox;
            } else {
                bbox = raster::BBox{0, 0, ctx.frame.width, ctx.frame.height};
            }
        } else {
            bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
        }

        if (!ctx.options.diagnostics_enabled) {
            bbox.clip_to(ctx.frame.width, ctx.frame.height);
        }
        if (!bbox.is_empty()) {
            x0 = std::min(x0, bbox.x0);
            y0 = std::min(y0, bbox.y0);
            x1 = std::max(x1, bbox.x1);
            y1 = std::max(y1, bbox.y1);
            has_any = true;
        }
    }

    if (!has_any) {
        return raster::BBox{0, 0, 0, 0};
    }
    return raster::BBox{x0, y0, x1, y1};
}

RenderNodeCachePolicy MultiSourceNode::cache_policy() const noexcept {
    return m_cache_policy;
}

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Strip the frame number — the items' full world matrices (embedded in
    // the loop below) already capture frame-to-frame animation changes, so
    // the frame dimension adds no useful discrimination.  This lets the LRU
    // cache reuse a rendered result across frames that share the same
    // effective transform (e.g. settled tail of an animation).
    key.frame = Frame{0};
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.options.modular_coordinates));

    // Hash every item's full world matrix and opacity so the cache key
    // changes when the layer-level animation (e.g. tracking_breathing)
    // produces a different transform.
    for (const auto& item : m_items) {
        key.params_hash = hash_combine(key.params_hash, hash_value(item.matrix));
        key.params_hash = hash_combine(key.params_hash, hash_value(item.opacity));
        // text_run items also fold the per-glyph animated state of
        // the underlying TextRunShape so the cache key invalidates when
        // `evaluate_animator_stack` mutates glyph state.  Without this
        // fold two animated frames with identical geometry would hit a
        // stale cache entry.
        if (item.node && item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            key.params_hash = hash_combine(
                key.params_hash,
                chronon3d::hash_text_run_shape(*item.node->shape.text_run_shape_handle().value));
        }
    }

    // 2.5D camera transform — invalidate when the camera moves so the bg
    // matrix inside compute_text_run_world_bbox / compute_world_bbox is
    // up-to-date.  Mirrors SourceNode::cache_key and TextRunNode::cache_key.
    if (m_uses_2_5d_projection && ctx.camera.has_camera_2_5d) {
        const auto& cam = ctx.camera.camera_2_5d;
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.position, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.rotation, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.zoom, sizeof(f32)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.fov_deg, sizeof(f32)));
        if (cam.point_of_interest_enabled) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.point_of_interest, sizeof(Vec3)));
        }
    }

    return key;
}

OwnedFB MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("multi_source_render", trace_category::kRasterize);
    auto fb = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, /*clear=*/true);

    if (ctx.resources.backend) {
        const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
        const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

        // text_run items are dispatched to `RenderBackend::draw_text_run`
        // instead of the generic `RenderBackend::draw_node` because the
        // former routes through the dedicated text-run processor with the
        // per-glyph transform stack.  The text is rasterized directly onto
        // the SHARED `*fb` so it composites SRC_OVER any earlier non-text
        // items in the same layer (vector order).

        for (const auto& item : m_items) {
            if (!item.node) continue;

            // ── text_run branch ─────────────────────────────────────
            // Dispatch via the virtual RenderBackend::draw_text_run when
            // the item carries ShapeType::TextRun.  Falls through
            // to the generic `draw_node` path when the backend doesn't
            // support text (draw_text_run returns false) so the layer still
            // produces partial output (shapes render; text is skipped).
            if (item.node->shape.type() == ShapeType::TextRun) {
                auto run_shape = item.node->shape.text_run_shape_handle().value;
                if (!run_shape) {
                    // Orphan text_run: an upstream source-pass already
                    // logs this once per layer (one-shot guard);
                    // here we silently skip so the layer still produces
                    // partial output for any non-text siblings on the
                    // shared framebuffer.
                    continue;
                }

                Mat4 world_matrix;
                if (m_uses_2_5d_projection || m_centered) {
                    world_matrix = canvas_center * ssaa_scale * item.matrix;
                } else {
                    world_matrix = ssaa_scale * item.matrix;
                }

                // PR2 — gate on capabilities().text_run first.  When the
                // active backend does not support text runs, emit a one-shot
                // warning and skip the per-item dispatch (avoids a
                // per-frame failed-virtual call).
                if (!ctx.resources.backend->capabilities().text_run) {
                    if (!m_backend_warned) {
                        spdlog::error(
                            "[multi-source] node='{}' contains text run items "
                            "but active backend does not support draw_text_run; "
                            "skipping text render.", m_name);
                        m_backend_warned = true;
                    }
                    continue;
                }

                auto result = ctx.resources.backend->draw_text_run(
                    *fb, *run_shape, world_matrix,
                    item.opacity);

                if (!result) {
                    spdlog::error(
                        "[multi-source] node='{}' text_run failed: [{}] {}",
                        m_name,
                        chronon3d::graph::render_backend_error_code_name(result.error().code),
                        result.error().message);
                }

                // NOTE: draw_text_run() already increments text_glyphs_rasterized
                // inside the processor.  Do NOT double-count here.

                if (ctx.options.diagnostics_enabled) {
                    spdlog::debug(
                        "[multi-source] node='{}' text_run drew={} glyphs={} "
                        "hash=0x{:016x} opacity={:.3f} tx={:.1f} ty={:.1f}",
                        m_name,
                        result ? result.value().items_drawn : 0u,
                        item.node->shape.text_run_shape_handle().value->glyphs.size(),
                        chronon3d::hash_text_run_shape(*item.node->shape.text_run_shape_handle().value),
                        item.opacity,
                        world_matrix[3][0],
                        world_matrix[3][1]
                    );
                }
                continue;  // text_run item is rendered; advance to next.
            }

            // ── regular (non-text-run) item ───────────────────────
            RenderState state;
            state.frame_number = static_cast<int>(ctx.frame.frame);
            state.ssaa_factor = ctx.options.ssaa_factor;
            if (m_uses_2_5d_projection) {
                state.matrix = canvas_center * ssaa_scale * item.matrix;
            } else {
                if (m_centered) {
                    state.matrix = canvas_center * ssaa_scale * item.matrix;
                } else {
                    state.matrix = ssaa_scale * item.matrix;
                }
            }

            state.opacity = item.opacity;
            state.world_matrix = item.matrix;
            state.clip_rect = ctx.tile.clip_rect;
            state.diagnostics_enabled = ctx.options.diagnostics_enabled;

            if (ctx.camera.has_camera_2_5d) {
                state.projection  = ctx.camera.projection_ctx;
            }

            ctx.resources.backend->draw_node(*fb, *item.node, state, ctx.camera.camera, ctx.frame.width, ctx.frame.height);
        }

        fb->set_opaque(false);
    }
    return fb;
}

} // namespace chronon3d::graph
