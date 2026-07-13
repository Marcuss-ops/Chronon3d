#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include "text_run/text_run_execution.hpp"
#include "text_run/text_run_transform.hpp"
#endif
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    RenderNodeCachePolicy policy
) : RenderGraphNode(policy), m_name(std::move(name)), m_items(std::move(items)), m_key(key) {}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));

    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    // CHRONON3D_PROJ_DIAG: indexed-for loop so item index reflects actual vector
    // position even when null items are skipped (range-for + manual counter was
    // bug-prone: counter incremented only in null-item-skip branch, leaving
    // bbox_i stale at 0 for non-null iterations where the diagnostic fires).
    for (std::size_t bbox_i = 0; bbox_i < m_items.size(); ++bbox_i) {
        const auto& item = m_items[bbox_i];
        if (!item.node) continue;
        Mat4 matrix;
        if (ctx.frame_input.has_camera_2_5d) {
            // Condition the 2.5D projection on the global `has_camera_2_5d`
            // trigger (Soluzione B).  Mirrors the SourceNode pattern for
            // key/pixel consistency.
            //
            // Dedup (round-2/3 code-reviewer #2 follow-up): the
            // projection+continue logic (from_mat4 + project_layer_2_5d +
            // CHRONON3D_PROJ_DIAG diagnostic + canvas_center*ssaa_scale
            // composition) is extracted to
            // `chronon3d::graph::detail::project_to_camera_space` in
            // `src/render_graph/nodes/detail/projection_helpers.hpp` and  // drift-allow: stale-ref
            // shared with the 2 source_node.cpp sites + the 2 sibling
            // multi_source_node sites below.  See the helper header for
            // the full design rationale.
            auto matrix_opt = chronon3d::graph::detail::project_to_camera_space(
                item.matrix, item.opacity, ctx, m_name, "predicted_bbox", bbox_i);
            if (!matrix_opt) {
                continue;
            }
            matrix = *matrix_opt;
        } else {
            // TICKET-TEXT-CLEANUP-5: centering is now baked into item.matrix
            // by the source pass / refresh.  m_centered removed.
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
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value, matrix, spread);
        } else
#endif
        // TICKET-122 FASE 3: GridPlane now uses 2.5D projection,
        // standard compute_world_bbox path (not native 3D).
        if (ctx.frame_input.has_camera_2_5d &&
            item.node->shape.type() == ShapeType::FakeBox3D) {
            if (auto proj_bbox = detail::projected_native_3d_bbox(ctx, *item.node, item.matrix, spread)) {
                bbox = *proj_bbox;
            } else {
                bbox = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
            }
        } else {
            bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
        }

        if (!ctx.policy.diagnostics_enabled) {
            bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
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

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Fase A6 — frame discriminator for animator-driven text changes.
    // With glyphs no longer mutated in-place, the text_run hash fold
    // below only captures doc-driven changes (Scramble/Morph/Crossfade).
    // Animator-driven changes (position/opacity/scale/blur per-frame)
    // need the frame number to invalidate the cache.  Fold the integral
    // frame so consecutive frames with different animator states don't
    // share a stale entry.
    key.params_hash = hash_combine(key.params_hash, hash_value(static_cast<u64>(ctx.frame_input.sample_time.integral_frame())));
    key.frame = ctx.frame_input.frame;
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));

    // Hash every item's full world matrix and opacity so the cache key
    // changes when the layer-level animation (e.g. tracking_breathing)
    // produces a different transform.
    for (const auto& item : m_items) {
        key.params_hash = hash_combine(key.params_hash, hash_value(item.matrix));
        key.params_hash = hash_combine(key.params_hash, hash_value(item.opacity));
#ifdef CHRONON3D_ENABLE_TEXT
        // text_run items also fold the per-glyph animated state of
        // the underlying TextRunShape so the cache key invalidates when
        // `evaluate_animator_stack` mutates glyph state.  Without this
        // fold two animated frames with identical geometry would hit a
        // stale cache entry.  PR 10: use the frame overload so
        // Scramble / Morph / CrossfadeLayouts / font-swap Cut frames
        // driven by an AnimatedTextDocument also invalidate correctly.
        if (item.node && item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            key.params_hash = hash_combine(
                key.params_hash,
                chronon3d::hash_text_run_shape(
                    *item.node->shape.text_run_shape_handle().value,
                    ctx.frame_input.sample_time.integral_frame()));
        }
#endif
    }

    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B).
    // Conditional on `has_camera_2_5d` globally so zoom-animated frames
    // (e.g. AE_CAM_02) produce distinct per-frame keys.
    if (ctx.frame_input.has_camera_2_5d) {
        cache::fold_camera_into_params_hash(key, ctx.frame_input.camera_2_5d);
    }

    return key;
}

NodeExecResult MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("multi_source_render", trace_category::kRasterize);

    // Fase A4 — null backend is a hard error (matches TextRunNode contract).
    if (!ctx.services.backend) {
        spdlog::error(
            "[multi-source] node='{}' cannot render: backend is null; "
            "aborting frame.", m_name);
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "backend is null"
        }};
    }

    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);

    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));

    // ── text_run items are dispatched to `RenderBackend::draw_text_run`
    // instead of the generic `RenderBackend::draw_node` because the
    // former routes through the dedicated text-run processor with the
    // per-glyph transform stack.  The text is rasterized directly onto
    // the SHARED `*fb` so it composites SRC_OVER any earlier non-text
    // items in the same layer (vector order).
    //
    // Fase A4 — error propagation unified with TextRunNode:
    //   - draw_text_run failure → NodeExecutionError (immediate return)
    //   - unsupported capability → NodeExecutionError
    //   - null shape → skip item (not an error — empty data)
    //
    // Regular items use draw_node() which returns void — tracked for
    // Phase C (requires coordinated API/ABI change).

    for (std::size_t i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            if (!item.node) continue;

#ifdef CHRONON3D_ENABLE_TEXT
            // ── text_run branch — unified via render_text_run_item ────
            if (item.node->shape.type() == ShapeType::TextRun) {
                auto run_shape = item.node->shape.text_run_shape_handle().value;
                if (!run_shape) {
                    continue;
                }

                if (!ctx.services.backend->capabilities().text_run) {
                    spdlog::error(
                        "[multi-source] node='{}' contains text run items "
                        "but active backend does not support draw_text_run; "
                        "aborting frame.", m_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        m_name,
                        "backend does not support draw_text_run"
                    }};
                }

                // 2.5D-aware placement: if camera is active, project
                // the item matrix; otherwise the source pass already
                // resolved the final matrix.
                Mat4 resolved_matrix = item.matrix;
                if (ctx.frame_input.has_camera_2_5d) {
                    auto proj_opt = chronon3d::graph::detail::project_to_camera_space(
                        item.matrix, item.opacity, ctx, m_name, "text_run_execute", i);
                    if (!proj_opt) {
                        continue;
                    }
                    resolved_matrix = *proj_opt;
                }

                auto result = text_run::render_text_run_item(
                    ctx, *ctx.services.backend, *fb, *run_shape,
                    TextRunPlacement{resolved_matrix}, item.opacity);

                if (!result) {
                    spdlog::error(
                        "[multi-source] node='{}' text_run failed: [{}] {}",
                        m_name,
                        chronon3d::graph::render_backend_error_code_name(result.error().code),
                        result.error().message);
                    return NodeExecResult{NodeExecutionError{
                        result.error().code,
                        m_name,
                        result.error().message
                    }};
                }

                if (result.value().actual_ink_bbox) {
                    ctx.node_exec.actual_ink_bbox = *result.value().actual_ink_bbox;
                }

                if (ctx.policy.diagnostics_enabled) {
                    spdlog::info(
                        "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) opacity={:.3f}",
                        ctx.frame_input.sample_time.integral_frame(),
                        m_name, i,
                        item.matrix[3][0], item.matrix[3][1], item.matrix[3][2],
                        item.opacity
                    );
                }
                continue;
            }
#endif

            // ── regular (non-text-run) item ───────────────────────
            // P0-1 note: draw_node() returns void — errors inside the backend
            // (e.g. missing processor-context) are logged but cannot be surfaced
            // to the executor.  Changing draw_node's signature to Result<>
            // requires a coordinated API/ABI change across SoftwareRenderer,
            // SoftwareBackend, and all test backends — deferred to Phase C.
            RenderState state;
            state.frame_number = static_cast<int>(ctx.frame_input.frame);
            state.ssaa_factor = ctx.policy.ssaa_factor;
            if (ctx.frame_input.has_camera_2_5d) {
                // TICKET-ae-cam-hash-collision Soluzione B
                // (MultiSourceNode consistency follow-up): same
                // global-trigger pattern as predicted_bbox site
                // above.  Dedup (round-2/3 code-reviewer #2): the
                // projection+continue logic is extracted to
                // `chronon3d::graph::detail::project_to_camera_space`
                // and shared across all 5 sites.  See the helper
                // header for the full design rationale.
                auto state_matrix_opt = chronon3d::graph::detail::project_to_camera_space(
                    item.matrix, item.opacity, ctx, m_name, "regular_execute", i);
                if (!state_matrix_opt) {
                    continue;
                }
                state.matrix = *state_matrix_opt;
            } else {
                // TICKET-TEXT-CLEANUP-5: centering is now baked into item.matrix
                // by the source pass / refresh.  m_centered removed.
                state.matrix = ssaa_scale * item.matrix;
            }

            state.opacity = item.opacity;
            state.world_matrix = item.matrix;
            state.clip_rect = ctx.node_exec.clip_rect;
            state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

            if (ctx.frame_input.has_camera_2_5d) {
                state.projection  = ctx.frame_input.projection_ctx;
            }

        ctx.services.backend->draw_node(*fb, *item.node, state, ctx.frame_input.camera, ctx.frame_input.width, ctx.frame_input.height);

        if (ctx.policy.diagnostics_enabled) {
            spdlog::info(
                "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) screen=({},{}) depth={} scale={} visible={}",
                ctx.frame_input.sample_time.integral_frame(),
                m_name,
                i,
                item.matrix[3][0], item.matrix[3][1], item.matrix[3][2],
                state.matrix[3][0], state.matrix[3][1],
                state.matrix[3][2],
                glm::length(Vec3(item.matrix[0])),
                true
            );
        }
    }

    fb->set_opaque(false);
    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
