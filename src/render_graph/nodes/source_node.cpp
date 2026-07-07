#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <limits>

namespace chronon3d::graph {

namespace {

constexpr f32 kSeedFrameEpsilon = 1e-3f;

[[nodiscard]] bool nearly_equal(f32 a, f32 b, f32 eps = kSeedFrameEpsilon) {
    return std::abs(a - b) <= eps;
}

[[nodiscard]] bool covers_full_frame_as_rectangle(
    const Mat4& matrix,
    f32 width,
    f32 height,
    bool centered = false
) {
    const f32 x0 = centered ? -width * 0.5f : 0.0f;
    const f32 x1 = centered ?  width * 0.5f : width;
    const f32 y0 = centered ? -height * 0.5f : 0.0f;
    const f32 y1 = centered ?  height * 0.5f : height;

    const Vec4 corners[4] = {
        matrix * Vec4(x0, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y1, 0.0f, 1.0f),
        matrix * Vec4(x0, y1, 0.0f, 1.0f)
    };

    std::array<f32, 4> xs{};
    std::array<f32, 4> ys{};
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (std::size_t i = 0; i < 4; ++i) {
        const auto& c = corners[i];
        if (std::abs(c.w) < 1e-6f) {
            return false;
        }

        xs[i] = c.x / c.w;
        ys[i] = c.y / c.w;
        min_x = std::min(min_x, xs[i]);
        min_y = std::min(min_y, ys[i]);
        max_x = std::max(max_x, xs[i]);
        max_y = std::max(max_y, ys[i]);
    }

    auto distinct_count = [](const std::array<f32, 4>& values) {
        std::array<f32, 4> unique{};
        std::size_t count = 0;
        for (f32 value : values) {
            bool seen = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (nearly_equal(value, unique[i])) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                unique[count++] = value;
            }
        }
        return count;
    };

    if (distinct_count(xs) != 2 || distinct_count(ys) != 2) {
        return false;
    }

    return nearly_equal(min_x, 0.0f) &&
           nearly_equal(min_y, 0.0f) &&
           nearly_equal(max_x, width) &&
           nearly_equal(max_y, height);
}

} // namespace

std::optional<raster::BBox> SourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
    Mat4 matrix;
    // TICKET-ae-cam-hash-collision Soluzione B (rendering-side) — mirror
    // the cache-key pattern: condition the 2.5D projection on the
    // *global* `has_camera_2_5d` trigger, NOT on the per-node
    // `m_uses_2_5d_projection` flag.  The per-node flag is `false` on
    // the SourceNode used by AE_CAM_02/04/07/09 (confirmed by the
    // existing inline comment in `cache_key()` below) so conditioning
    // on it would make the projection branch unreachable for the very
    // scenes the cache key fix was designed to differentiate.  Only
    // FakeBox3D is excluded — it routes through
    // `detail::projected_native_3d_bbox` further down and expects an
    // unprojected world matrix there.  GridPlane was formerly excluded
    // too (TICKET-122 FASE 3: removed so grid scales with zoom).
    const ShapeType shape_type = m_node.shape.type();
    // TICKET-122 FASE 3: GridPlane (grid_background) now participates
    // in 2.5D projection so the grid scales with zoom.  Previously
    // excluded, it always rendered full-canvas identically regardless
    // of zoom — the root cause of AE_CAM_02 hash collision.
    const bool apply_2_5d_projection =
        ctx.frame_input.has_camera_2_5d &&
        shape_type != ShapeType::FakeBox3D;
    if (apply_2_5d_projection) {
        // TICKET-ae-cam-hash-collision SourceNode forward-fix (per
        // ## Verification gap): pass the actual layer TRS to
        // project_layer_2_5d, NOT a default-constructed Transform
        // (which would collapse `input.layer_size = (1,1)` and cause
        // the projected bbox to be sub-pixel-clipped at the rasterizer,
        // rendering 2D layers as transparent-black and producing
        // framebuffer_hash collisions).
        //
        // Dedup (round-2/3 code-reviewer #2 follow-up): the
        // projection+continue logic is extracted to
        // `chronon3d::graph::detail::project_to_camera_space` in
        // `src/render_graph/nodes/detail/projection_helpers.hpp` and
        // shared with the 3 multi_source_node.cpp sites.  This handles
        // the m_matrix_override case correctly: base_matrix is either
        // the override or the world transform's matrix, and from_mat4
        // (called inside the helper) decomposes the source of truth.
        //  m_opacity_override.value_or(m_node.world_transform.opacity)
        //  precedence is preserved (override > world transform).
        auto matrix_opt = chronon3d::graph::detail::project_to_camera_space(
            base_matrix, m_opacity_override.value_or(m_node.world_transform.opacity),
            ctx, m_name, "predicted_bbox");
        if (!matrix_opt) {
            // Behind camera / off frustum: return no bbox so the graph
            // aggressively culls the node (saves context acquisition
            // overhead).  Native 3D shapes are excluded above so this
            // path is only hit for 2.5D-projected 2D layers.  The
            // CHRONON3D_PROJ_DIAG diagnostic has already been emitted
            // by the helper.
            return std::nullopt;
        }
        matrix = *matrix_opt;
    } else {
        // TICKET-TEXT-CLEANUP-5: centering is now baked into matrix_override
        // by the source pass / refresh.  m_centered removed.
        matrix = ssaa_scale * base_matrix;
    }

    f32 spread = 0.0f;
    if (m_node.shadow.enabled) {
        spread = std::max(spread, m_node.shadow.radius + std::max(std::abs(m_node.shadow.offset.x), std::abs(m_node.shadow.offset.y)));
    }
    if (m_node.glow.enabled) {
        spread = std::max(spread, m_node.glow.radius);
    }
    spread += 8.0f;

    // TICKET-122 FASE 3: GridPlane now goes through 2.5D projection above,
    // so it uses the standard compute_world_bbox path (not native 3D).
    if (ctx.frame_input.has_camera_2_5d &&
        m_node.shape.type() == ShapeType::FakeBox3D) {
        const Mat4 world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        if (auto bbox = detail::projected_native_3d_bbox(ctx, m_node, world_matrix, spread)) {
            return bbox;
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    auto bbox = renderer::compute_world_bbox(m_node.shape, matrix, spread);
    if (!ctx.policy.diagnostics_enabled) {
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
    }
    if (bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return bbox;
}

cache::NodeCacheKey SourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // TICKET-122: use the current evaluation frame instead of the
    // build-time frame (always Frame{0} for frame-variant nodes),
    // so the cache key differentiates between frames even when
    // params_hash alone would collide (e.g. zoom-identical states).
    key.frame = ctx.frame_input.frame;
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }
    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B).
    // Conditional on `has_camera_2_5d` (NOT on `m_uses_2_5d_projection`) —
    // the bug in tickets `TICKET-AE-CAM-PRECISION-COLLAPSE` and
    // TICKET-ae-cam-hash-collision was precisely that the prior gate made
    // fingerprinting conditional on the per-node flag, so AE_CAM_02 frame
    // 0 / 30 / 60 (`m_uses_2_5d_projection == false` on the
    // SourceNode used by `ae_cam_02_zoom_fov`) collided on the cache key.
    if (ctx.frame_input.has_camera_2_5d) {
        cache::fold_camera_into_params_hash(key, ctx.frame_input.camera_2_5d);
    }
    return key;
}

NodeExecResult SourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("source_render", trace_category::kRasterize);
    const bool full_frame_seed = can_seed_full_frame(ctx);

    // Skip clear when full-frame opaque with integer translation — no
    // sub-pixel gaps are possible because the source covers every pixel
    // and the composite path uses integer-rounded coordinates.
    bool skip_clear = false;
    if (full_frame_seed) {
        const auto mat = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        // Identity scale + rotation (coefficients ~1 or ~0) and integer translation
        const bool identity_scale_rot =
            std::abs(mat[0][0] - 1.0f) < 1e-4f && std::abs(mat[0][1]) < 1e-4f &&
            std::abs(mat[1][0]) < 1e-4f && std::abs(mat[1][1] - 1.0f) < 1e-4f;
        if (identity_scale_rot) {
            const float tx = mat[3][0];
            const float ty = mat[3][1];
            skip_clear = std::abs(tx - std::round(tx)) < 1e-4f &&
                         std::abs(ty - std::round(ty)) < 1e-4f;
        }
    }

    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, !skip_clear);
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());

    if (ctx.services.backend) {
        RenderState state;
        state.frame_number = static_cast<int>(ctx.frame_input.frame);
        state.ssaa_factor = ctx.policy.ssaa_factor;

        // TICKET-ae-cam-hash-collision Soluzione B (rendering-side) —
        // mirror the cache-key pattern: condition the 2.5D projection
        // on the *global* `has_camera_2_5d` trigger, NOT on the
        // per-node `m_uses_2_5d_projection` flag.  See the matching
        // comment in `predicted_bbox` above for the rationale.  Only
        // FakeBox3D is excluded (GridPlane now projected per TICKET-122
        // FASE 3).
        // TICKET-122 FASE 3: GridPlane participates in 2.5D projection
        // so the grid scales with zoom (matches predicted_bbox above).
        const ShapeType exec_shape_type = m_node.shape.type();
        const bool exec_apply_2_5d_projection =
            ctx.frame_input.has_camera_2_5d &&
            exec_shape_type != ShapeType::FakeBox3D;
        if (exec_apply_2_5d_projection) {
            // TICKET-ae-cam-hash-collision SourceNode forward-fix (per
            // ## Verification gap): same `from_mat4(base_matrix, ...)`
            // pattern as predicted_bbox site above.  See site-1 comment
            // for the full rationale (extracts actual layer scale from
            // base_matrix's column vectors, pre-empting the
            // empty-Transform-tr transparent-black bug).
            //
            // Dedup (round-2/3 code-reviewer #2 follow-up): the
            // projection+continue logic is extracted to
            // `chronon3d::graph::detail::project_to_camera_space` and
            // shared with the 3 multi_source_node sites + the sibling
            // predicted_bbox site.  The helper emits the
            // CHRONON3D_PROJ_DIAG diagnostic internally.  After the
            // helper returns nullopt, this site adds the
            // `[source-skip]` per-site diagnostic (gated on
            // `ctx.policy.diagnostics_enabled`, not on
            // CHRONON3D_PROJ_DIAG) + the defensive `fb->set_opaque(false)`
            // + the early-return of the cleared fb.
            auto state_matrix_opt = chronon3d::graph::detail::project_to_camera_space(
                base_matrix, m_opacity_override.value_or(m_node.world_transform.opacity),
                ctx, m_name, "execute");
            if (!state_matrix_opt) {
                // Layer is behind camera plane / off frustum: skip the
                // draw call and return the empty (cleared) fb so the
                // graph continues.  Matches MultiSourceNode skip-path
                // semantics (execute continue;).
                if (ctx.policy.diagnostics_enabled) {
                    spdlog::info(
                        "[source-skip] node='{}' proj.visible=false frame={} — returning empty fb",
                        m_name,
                        ctx.frame_input.sample_time.integral_frame());
                }
                fb->set_opaque(false);  // empty fb is not opaque (defensive)
                return NodeExecResult{std::move(fb)};
            }
            state.matrix = *state_matrix_opt;
        } else {
            // TICKET-TEXT-CLEANUP-5: centering is now baked into matrix_override
            // by the source pass / refresh.  m_centered removed.
            state.matrix = ssaa_scale * base_matrix;
        }
        state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
        state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        state.frame_number = static_cast<int>(ctx.frame_input.frame);

        state.clip_rect = ctx.node_exec.clip_rect;
        state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

        if (ctx.frame_input.has_camera_2_5d) {
            state.projection  = ctx.frame_input.projection_ctx;
        }

        // P0-1: draw_node() returns void — backend failures (e.g. missing
        // processor-context, unsupported shape) are logged but cannot propagate
        // to the executor.  Tracked for Phase C post-freeze.
        ctx.services.backend->draw_node(*fb, m_node, state, ctx.frame_input.camera, ctx.frame_input.width, ctx.frame_input.height);
        fb->set_opaque(full_frame_seed);

        if (ctx.policy.diagnostics_enabled) {
            int nonzero_pixels = 0;
            for (i32 y = 0; y < fb->height(); ++y) {
                const Color* row = fb->pixels_row(y);
                for (i32 x = 0; x < fb->width(); ++x) {
                    const Color& c = row[x];
                    if (c.a > 0.001f || c.r > 0.001f || c.g > 0.001f || c.b > 0.001f) {
                        ++nonzero_pixels;
                    }
                }
            }

            spdlog::info(
                "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f} det2d={:.6f}",
                m_name,
                static_cast<int>(m_node.shape.type()),
                nonzero_pixels,
                state.opacity,
                state.matrix[3][0],
                state.matrix[3][1],
                glm::determinant(glm::mat3(
                    state.matrix[0][0], state.matrix[0][1], state.matrix[0][3],
                    state.matrix[1][0], state.matrix[1][1], state.matrix[1][3],
                    state.matrix[3][0], state.matrix[3][1], state.matrix[3][3]
                ))
            );
        }
    }
    return NodeExecResult{std::move(fb)};
}

bool SourceNode::can_seed_full_frame(const RenderGraphContext& ctx) const noexcept {
    // Frame-invariant + reusable across frames = eligible to skip the clear.
    // TICKET-ae-cam-hash-collision Soluzione B (rendering-side) — bail
    // when a camera is active even if the per-node flag is false: with
    // a 2.5D camera the screen-space "full frame" assumption no longer
    // holds (zoom changes the effective coverage), so full-frame
    // seeding would produce stale FBs that bypass the cache-key fix.
    // TICKET-TEXT-CLEANUP-8: m_uses_2_5d_projection removed.  2.5D
    // projection is now conditioned on has_camera_2_5d globally.
    if (!m_cache_policy.reusable_across_frames()
        || ctx.frame_input.has_camera_2_5d) {
        return false;
    }

    if (m_node.shape.type() != ShapeType::Image) {
        return false;
    }

    const auto& img = m_node.shape.image();
    const auto& tr = m_node.world_transform;
    const f32 opacity = m_opacity_override.value_or(tr.opacity);

    if (ctx.node_exec.clip_rect) {
        const bool clip_is_full = ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                  ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height;
        if (!clip_is_full) {
            return false;
        }
    }

    const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.frame_input.width)) < kSeedFrameEpsilon &&
                           std::abs(img.size.y - static_cast<f32>(ctx.frame_input.height)) < kSeedFrameEpsilon;
    const bool opaque = img.opacity >= 0.999f && opacity >= 0.999f;
    if (!full_size || !opaque) {
        return false;
    }

    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 local_matrix = m_matrix_override.value_or(tr.to_mat4());
    // TICKET-TEXT-CLEANUP-5: centering is now baked into matrix_override
    // by the source pass / refresh.  m_centered removed.
    const Mat4 effective_matrix = ssaa_scale * local_matrix;

    return covers_full_frame_as_rectangle(effective_matrix, static_cast<f32>(ctx.frame_input.width), static_cast<f32>(ctx.frame_input.height), false);
}

} // namespace chronon3d::graph
