#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#endif
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    bool centered, bool uses_2_5d_projection, RenderNodeCachePolicy policy
) : RenderGraphNode(policy), m_name(std::move(name)), m_items(std::move(items)), m_key(key),
    m_centered(centered), m_uses_2_5d_projection(uses_2_5d_projection) {}

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
        if (m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d) {
            // Cat-1 fix: when 2.5D projection is enabled AND a camera is
            // supplied by frame_input, the layer's world TRS alone is not
            // enough — we still need proj * view * layerTRS so the layer
            // lands at the camera-projected screen position.  Without this
            // branch, items rendered with anchored origin (default
            // anchor=(0,0,0)) would clip into the canvas top-left
            // (e.g. rect 100x100 at world (0,0,0) with default zoom 1000
            // falls back to top-left instead of canvas center).
            chronon3d::Transform tr;
            auto proj = chronon3d::project_layer_2_5d(
                tr, item.matrix,
                ctx.frame_input.camera_2_5d,
                static_cast<f32>(ctx.frame_input.width),
                static_cast<f32>(ctx.frame_input.height),
                false);
            if (!proj.visible) {
                // CHRONON3D_PROJ_DIAG: per-frame diagnostic for the proj.visible=false
                // skip path in predicted_bbox. Gated on env var so zero-cost when unset;
                // revertable by `unset CHRONON3D_PROJ_DIAG`. Emits user-spec fields:
                // proj.layer_* + camera.{position,zoom,fov} + world_z_depth + frame.
                if (std::getenv("CHRONON3D_PROJ_DIAG")) {
                    spdlog::warn("[PROJ_DIAG] branch=SKIP_NOT_VISIBLE stage=predicted_bbox node='{}' item#{} world_z_depth={:.1f} proj.depth={:.4f} proj.perspective_scale={:.4f} proj.position=({:.1f},{:.1f}) proj.scale=({:.4f},{:.4f}) camera.position=({:.1f},{:.1f},{:.1f}) camera.zoom={:.1f} camera.fov_deg={:.1f} frame={}",
                        m_name, bbox_i,
                        item.matrix[3][2],
                        proj.depth, proj.perspective_scale,
                        proj.transform.position.x, proj.transform.position.y,
                        proj.transform.scale.x, proj.transform.scale.y,
                        ctx.frame_input.camera_2_5d.position.x, ctx.frame_input.camera_2_5d.position.y, ctx.frame_input.camera_2_5d.position.z,
                        ctx.frame_input.camera_2_5d.zoom, ctx.frame_input.camera_2_5d.fov_deg,
                        ctx.frame_input.sample_time.integral_frame());
                }
                continue;
            }
            matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
        } else if (m_uses_2_5d_projection || m_centered) {
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
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value, matrix, spread);
        } else
#endif
        if (ctx.frame_input.has_camera_2_5d &&
            (item.node->shape.type() == ShapeType::FakeBox3D || item.node->shape.type() == ShapeType::GridPlane)) {
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
    key.frame = Frame{0};
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

    // 2.5D camera transform — invalidate when the camera moves so the bg
    // matrix inside compute_text_run_world_bbox / compute_world_bbox is
    // up-to-date.  Mirrors SourceNode::cache_key and TextRunNode::cache_key.
    if (m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d) {
        const auto& cam = ctx.frame_input.camera_2_5d;
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
            // ── text_run branch (Fase A6: clone-before-mutate) ─────
            if (item.node->shape.type() == ShapeType::TextRun) {
                auto run_shape = item.node->shape.text_run_shape_handle().value;
                if (!run_shape) {
                    continue;
                }
                // Fase A6 — never mutate the shared shape. Clone glyphs
                // locally, evaluate into the clone, pass clone to backend.
                TextRunShape local_shape = *run_shape;
                chronon3d::update_text_run_shape_per_frame(local_shape, ctx.frame_input.sample_time);

                Mat4 world_matrix;
                if (m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d) {
                    // Cat-1 fix: same camera-in-projection treatment as
                    // regular items (mirror of predicted_bbox/execute
                    // regular branch).
                    chronon3d::Transform tr;
                    auto proj = chronon3d::project_layer_2_5d(
                        tr, item.matrix,
                        ctx.frame_input.camera_2_5d,
                        static_cast<f32>(ctx.frame_input.width),
                        static_cast<f32>(ctx.frame_input.height),
                        false);
                    if (!proj.visible) {
                        // CHRONON3D_PROJ_DIAG: per-frame diagnostic for the proj.visible=false
                        // skip path in text_run execute branch. Gated on env var so
                        // zero-cost when unset; revertable by `unset CHRONON3D_PROJ_DIAG`.
                        // Emits user-spec fields: proj.layer_* + camera.{position,zoom,fov} + world_z_depth + frame.
                        if (std::getenv("CHRONON3D_PROJ_DIAG")) {
                            spdlog::warn("[PROJ_DIAG] branch=SKIP_NOT_VISIBLE stage=text_run_execute node='{}' item#{} world_z_depth={:.1f} proj.depth={:.4f} proj.perspective_scale={:.4f} proj.position=({:.1f},{:.1f}) proj.scale=({:.4f},{:.4f}) camera.position=({:.1f},{:.1f},{:.1f}) camera.zoom={:.1f} camera.fov_deg={:.1f} frame={}",
                                m_name, i,
                                item.matrix[3][2],
                                proj.depth, proj.perspective_scale,
                                proj.transform.position.x, proj.transform.position.y,
                                proj.transform.scale.x, proj.transform.scale.y,
                                ctx.frame_input.camera_2_5d.position.x, ctx.frame_input.camera_2_5d.position.y, ctx.frame_input.camera_2_5d.position.z,
                                ctx.frame_input.camera_2_5d.zoom, ctx.frame_input.camera_2_5d.fov_deg,
                                ctx.frame_input.sample_time.integral_frame());
                        }
                        continue;
                    }
                    world_matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
                } else if (m_uses_2_5d_projection || m_centered) {
                    world_matrix = canvas_center * ssaa_scale * item.matrix;
                } else {
                    world_matrix = ssaa_scale * item.matrix;
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

                auto result = ctx.services.backend->draw_text_run(
                    *fb, local_shape, world_matrix,
                    item.opacity);

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

                if (ctx.policy.diagnostics_enabled) {
                    // Canonical per-item log — text_run branch.  screen=
                    // derived from world_matrix (proj*view*layerTRS +
                    // canvas_center + ssaa_scale), the same chain as
                    // state.matrix in the regular branch, so the two
                    // log blocks stay bit-equivalent across greps.
                    spdlog::info(
                        "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) screen=({},{}) depth={} scale={} visible={}",
                        ctx.frame_input.sample_time.integral_frame(),
                        m_name,
                        i,
                        item.matrix[3][0], item.matrix[3][1], item.matrix[3][2],
                        world_matrix[3][0], world_matrix[3][1],
                        world_matrix[3][2],
                        glm::length(Vec3(item.matrix[0])),
                        true
                    );
                    spdlog::debug(
                        "[multi-source] node='{}' text_run drew={} glyphs={} "
                        "hash=0x{:016x} opacity={:.3f} tx={:.1f} ty={:.1f}",
                        m_name,
                        result ? result.value().items_drawn : 0u,
                        item.node->shape.text_run_shape_handle().value->glyphs.size(),
                        chronon3d::hash_text_run_shape(
                            local_shape,
                            ctx.frame_input.sample_time.integral_frame()),
                        item.opacity,
                        world_matrix[3][0],
                        world_matrix[3][1]
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
            if (m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d) {
                // Cat-1 fix: compute proj*view*layerTRS at render time so
                // anchor-aware items render at the camera-projected screen
                // position.  When the layer is behind the camera plane or
                // off the frustum, we skip the item entirely (proj.visible
                // would be false) so it does not pop into the top-left of
                // the canvas (regression where the bbox-equivalent matrix
                // would otherwise be identity and the rect's local origin
                // would clip at (0,0)).
                chronon3d::Transform tr;
                auto proj = chronon3d::project_layer_2_5d(
                    tr, item.matrix,
                    ctx.frame_input.camera_2_5d,
                    static_cast<f32>(ctx.frame_input.width),
                    static_cast<f32>(ctx.frame_input.height),
                    false);
                if (!proj.visible) {
                    // CHRONON3D_PROJ_DIAG: per-frame diagnostic for the proj.visible=false
                    // skip path in regular execute branch. Gated on env var so zero-cost
                    // when unset; revertable by `unset CHRONON3D_PROJ_DIAG`.
                    // Emits user-spec fields: proj.layer_* + camera.{position,zoom,fov} + world_z_depth + frame.
                    if (std::getenv("CHRONON3D_PROJ_DIAG")) {
                        spdlog::warn("[PROJ_DIAG] branch=SKIP_NOT_VISIBLE stage=regular_execute node='{}' item#{} world_z_depth={:.1f} proj.depth={:.4f} proj.perspective_scale={:.4f} proj.position=({:.1f},{:.1f}) proj.scale=({:.4f},{:.4f}) camera.position=({:.1f},{:.1f},{:.1f}) camera.zoom={:.1f} camera.fov_deg={:.1f} frame={}",
                            m_name, i,
                            item.matrix[3][2],
                            proj.depth, proj.perspective_scale,
                            proj.transform.position.x, proj.transform.position.y,
                            proj.transform.scale.x, proj.transform.scale.y,
                            ctx.frame_input.camera_2_5d.position.x, ctx.frame_input.camera_2_5d.position.y, ctx.frame_input.camera_2_5d.position.z,
                            ctx.frame_input.camera_2_5d.zoom, ctx.frame_input.camera_2_5d.fov_deg,
                            ctx.frame_input.sample_time.integral_frame());
                    }
                    continue;
                }
                state.matrix = canvas_center * ssaa_scale * proj.transform.to_mat4();
            } else if (m_uses_2_5d_projection) {
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
