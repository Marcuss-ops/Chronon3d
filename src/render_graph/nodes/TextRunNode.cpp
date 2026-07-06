// SPDX-License-Identifier: MIT
//
// TextRunNode — orchestrator (M1.5#1).  The hard logic that used to
// live inline (per-frame shape clone, pre-dispatch validation, world
// transform, draw dispatch, diagnostic emit) is now extracted into
// single-responsibility helpers in `text_run/`.  `execute()` reads
// as a 5-stage pipeline:
//
//   0. defensive missing-shape early-return
//   1. acquire canvas framebuffer
//   2. validate (null backend / unsupported capability)  → NodeExecutionError
//   3. prepare per-frame shape + build world matrix + opacity
//   4. dispatch draw_text_run + surface backend errors  → NodeExecutionError
//   5. optional diagnostic-mode spdlog::debug
//
// `predicted_bbox()` and `cache_key()` are unchanged in behaviour
// (preserving all regression locks in test_protected_core_contracts.cpp);
// `predicted_bbox` now reuses `text_run::build_world_matrix` so the
// canvas coordinate space is identical to `execute()` by
// construction (deduplication of the SSAA + canvas-center branch).

#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#endif
// M1.5#1 — internal helpers under src/render_graph/nodes/text_run/
// (NOT under include/chronon3d/).  Same-directory-relative include
// matches the convention used by nodes/transform_kernels.cpp for
// nodes/sampling_utils.hpp.
#include "text_run/text_run_execution.hpp"
#include "text_run/text_run_transform.hpp"
#include "text_run/text_run_diagnostics.hpp"
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph {

// =============================================================================
// Constructor
// =============================================================================

TextRunNode::TextRunNode(
    std::string name,
    std::shared_ptr<TextRunShape> shape,
    const ::chronon3d::RenderNode& render_ref,
    const cache::NodeCacheKey& key,
    bool centered,
    bool uses_2_5d_projection,
    std::optional<Mat4> matrix_override,
    std::optional<f32> opacity_override,
    RenderNodeCachePolicy policy
)
    : RenderGraphNode(policy),
      m_name(std::move(name)),
      m_shape(std::move(shape)),
      m_render_ref(render_ref),
      m_key(key),
      m_centered(centered),
      m_uses_2_5d_projection(uses_2_5d_projection),
      m_matrix_override(std::move(matrix_override)),
      m_opacity_override(opacity_override)
{}

// =============================================================================
// predicted_bbox
// =============================================================================
//
// 2.5D-aware predicted bbox via renderer's `compute_text_run_world_bbox`.
// Matrix composition is delegated to `text_run::build_world_matrix()` so
// the bbox sampling and the rasterization dispatch agree on the canvas
// coordinate space by construction.

std::optional<raster::BBox> TextRunNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) const {
    if (!m_shape) {
        return std::nullopt;
    }

    const Mat4 matrix = text_run::build_world_matrix(
        m_render_ref, ctx, m_uses_2_5d_projection, m_centered, m_matrix_override);

    f32 spread = 0.0f;
    if (m_render_ref.shadow.enabled) {
        spread = std::max(spread,
            m_render_ref.shadow.radius
            + std::max(std::abs(m_render_ref.shadow.offset.x),
                       std::abs(m_render_ref.shadow.offset.y)));
    }
    if (m_render_ref.glow.enabled) {
        spread = std::max(spread, m_render_ref.glow.radius);
    }
    spread += 8.0f;

#ifdef CHRONON3D_ENABLE_TEXT
    auto bbox = renderer::compute_text_run_world_bbox(*m_shape, matrix, spread);
#else
    auto bbox = renderer::compute_world_bbox(m_render_ref.shape, matrix, spread);
#endif

    // TICKET-104 DIAGNOSTIC — one-shot runtime log of the predicted
    // bbox computation + clip-to-canvas decision.  *** REMOVE once root
    // cause is confirmed. ***
    // Critical for isolating the rasterization gap: if the fix's matrix
    // (translate(1920, 1080)) puts the world bbox off-canvas, the
    // clip_to() makes the bbox empty, and the function returns a zero
    // bbox.  The executor likely interprets a zero bbox as "no work to
    // do" and skips execute() entirely, which would explain why
    // prepare_per_frame_shape and draw_text_run diagnostics never fire.
    static bool kTicket104PbxOneShot = true;
    if (kTicket104PbxOneShot) {
        const bool diag = ctx.policy.diagnostics_enabled;
        const f32 mm_t_x = matrix[3][0];
        const f32 mm_t_y = matrix[3][1];
        spdlog::info(
            "[TICKET104:predicted_bbox] "
            "matrix.t=({:.2f},{:.2f}) spread={:.2f} "
            "world_bbox=({:d},{:d},{:d},{:d}) "
            "diagnostics_enabled={} canvas=({:.0f},{:.0f})",
            mm_t_x, mm_t_y, spread,
            bbox.x0, bbox.y0, bbox.x1, bbox.y1,
            diag ? 1 : 0,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height)
        );
    }

    if (!ctx.policy.diagnostics_enabled) {
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
    }
    if (bbox.is_empty()) {
        if (kTicket104PbxOneShot) {
            spdlog::info(
                "[TICKET104:predicted_bbox] RETURN zero-bbox "
                "(executor likely skips execute())");
            kTicket104PbxOneShot = false;
        }
        return raster::BBox{0, 0, 0, 0};
    }
    if (kTicket104PbxOneShot) {
        spdlog::info(
            "[TICKET104:predicted_bbox] RETURN bbox=({:d},{:d},{:d},{:d})",
            bbox.x0, bbox.y0, bbox.x1, bbox.y1
        );
        kTicket104PbxOneShot = false;
    }
    return bbox;
}

// =============================================================================
// cache_key
// =============================================================================

cache::NodeCacheKey TextRunNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;

#ifdef CHRONON3D_ENABLE_TEXT
    if (m_shape) {
        key.params_hash = hash_combine(
            key.params_hash,
            chronon3d::hash_text_run_shape(
                *m_shape,
                ctx.frame_input.sample_time.integral_frame()));
    } else {
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(0xdeadbeef));
    }
#else
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(0xdeadbeef));
#endif

    key.source_hash = hash_combine(
        key.source_hash,
        hash_combine(
            hash_string(m_name),
            hash_bytes(&m_render_ref.world_transform.position,
                       sizeof(Vec3))));

    key.params_hash = hash_combine(
        key.params_hash,
        static_cast<u64>(ctx.policy.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }

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

// =============================================================================
// execute — M1.5#1 ORCHESTRATOR
// =============================================================================
//
// Reads as a 5-stage pipeline.  Every stage delegates to a single
// helper in text_run/; NODE-LEVEL BOOKKEEPING (acquire fb, route
// success / failure, dispatch-move, surface error) stays here so the
// NodeExecResult contract is preserved end-to-end.
//
// Pre-condition: covered by tests/render_graph/nodes/test_text_run_node_execute_error.cpp
// (existing — diagnostic-string lock) AND tests/render_graph/nodes/test_text_run_node_return_channel.cpp
// (new — return-channel contract).

NodeExecResult TextRunNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> /*inputs*/,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) {
    CHRONON_ZONE_C("text_run_render", trace_category::kRasterize);

    // ── 0. Defensive: missing shape (source-pass only emits when set). ──
    if (!m_shape) {
        return NodeExecResult{
            ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true)};
    }

    // ── 1. Acquire canvas framebuffer (no clear-skip — text can't fill a frame). ──
    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);

    // ── 2. Validate pre-dispatch invariants. ──
    auto* backend = ctx.services.backend;
    if (auto err = text_run::validate_execution(backend, m_name)) {
        return NodeExecResult{std::move(*err)};
    }

    // ── 3. Prepare per-frame shape (A6 immutability) + world transform + opacity. ──
#ifdef CHRONON3D_ENABLE_TEXT
    const TextRunShape eval_shape =
        text_run::prepare_per_frame_shape(*m_shape, ctx.frame_input.sample_time);
#else
    const TextRunShape& eval_shape = *m_shape;
#endif

    const Mat4 world_matrix = text_run::build_world_matrix(
        m_render_ref, ctx, m_uses_2_5d_projection, m_centered, m_matrix_override);
    const f32 opacity = m_opacity_override.value_or(m_render_ref.world_transform.opacity);

    // ── 4. Dispatch draw_text_run + surface backend error. ──
    auto dispatch = backend->draw_text_run(*fb, eval_shape, world_matrix, opacity);
    if (!dispatch) {
        text_run::report_failure(
            m_name,
            dispatch.error().code,
            render_backend_error_code_name(dispatch.error().code),
            dispatch.error().message);
        return NodeExecResult{NodeExecutionError{
            dispatch.error().code,
            m_name,
            std::string(dispatch.error().message)
        }};
    }

    // ── 5. Per-frame debug diagnostic (opt-in via ctx.policy.diagnostics_enabled). ──
    if (ctx.policy.diagnostics_enabled) {
#ifdef CHRONON3D_ENABLE_TEXT
        text_run::report_diagnostic(
            m_name, eval_shape, dispatch.value().items_drawn, opacity, world_matrix,
            chronon3d::hash_text_run_shape(
                eval_shape, ctx.frame_input.sample_time.integral_frame()));
#else
        text_run::report_diagnostic(
            m_name, eval_shape, dispatch.value().items_drawn, opacity, world_matrix,
            std::nullopt);
#endif
    }

    // NOTE: draw_text_run() already increments text_glyphs_rasterized
    // inside the processor.  Do NOT double-count here — the processor
    // is the single source of truth for telemetry.

    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
