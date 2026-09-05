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
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
// Private definition needed by the per-session warn-once helper below.
#include "../executor/text_bbox_reporter.hpp"

namespace {
// Convert optional renderer::TextRunLocalBounds to the project's canonical Rect.
// Lives in an anonymous namespace to keep the symbol local to this TU.
chronon3d::Rect local_bounds_to_rect(
    const std::optional<chronon3d::renderer::TextRunLocalBounds>& bounds) {
    if (!bounds) {
        return chronon3d::Rect{};
    }
    return chronon3d::renderer::to_rect(*bounds);
}

// ------------------------------------------------------------------
// TICKET-FIX-ALPHA-SCANNER-DUP-V1 — per-session warn-once (TU-local).
//
// Consolidates the dedup-decision pattern that previously appeared
// inlined at both the CONSERVATIVE_EXPAND and FU04_EXPAND sites in
// `predicted_bbox()` below.  Replaces the inline duplication with a
// single canonical helper (Cat-3 anti-dup: dedup logic declared
// ONCE; the message strings still live at the call sites so
// `rg '\bCONSERVATIVE_EXPAND\b'` / `rg '\bFU04_EXPAND\b'` continues
// to discover them).
//
// Behaviour preserved from the original inlined pattern:
//   - null reporter → suppress dedup (always emit; standalone test
//     paths drive `predicted_bbox()` without an executor and need
//     every warning to surface);
//   - wired reporter → exactly-once per session via the atomic
//     `TextBboxReporter::has_warned()` + `mark_warned()` pair
//     (closes §honesty defects: data race on parallel render and
//     first-error-masking later invocations).
// ------------------------------------------------------------------
template <typename... Args>
inline void text_bbox_warn_once(
    chronon3d::graph::TextBboxReporter* reporter,
    std::string_view fmt_str, Args&&... args)
{
    if (reporter && reporter->has_warned()) {
        return;
    }
    spdlog::warn(fmt::runtime(fmt_str.data()), std::forward<Args>(args)...);
    if (reporter) {
        reporter->mark_warned();
    }
}

}  // anonymous namespace
// M1.5#1 — internal helpers under src/render_graph/nodes/text_run/
// (NOT under include/chronon3d/).  Same-directory-relative include
// matches the convention used by nodes/transform_kernels.cpp for
// nodes/sampling_utils.hpp.
#include "text_run/text_run_execution.hpp"
#include "detail/producer_surface_bounds.hpp"
#include "text_run/text_run_transform.hpp"
#include "text_run/text_run_diagnostics.hpp"
#ifdef CHRONON3D_ENABLE_TEXT
#include "text_run/text_run_debug_overlay.hpp"
#endif
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include <chronon3d/text/text_visibility_audit.hpp>

namespace {
// TU-scoped warn-once deduper for `verify_text_visibility()` (Step 2
// fix (c)). Replaces the implicit process-wide static-bool pattern that
// previously existed inside the audit; behaviorally equivalent
// (one-shot per `(node_id, TextWarningKind)` for the entire node's
// lifetime) while closing the §honesty defects (data race on parallel
// render; first-error masking later invocations). The deduper is
// TU-private by virtue of the anonymous namespace; render-graph and
// CLI sub-targets each have their own static instance.
chronon3d::WarnOnceDeduper s_warn_deduper;
}  // anonymous namespace
#endif
#include <spdlog/spdlog.h>

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d::graph {

// =============================================================================
// Constructor
// =============================================================================

#include "text_run/text_run_node_geometry.inc"

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
    CHRONON_TRACE_SCOPE("chronon.node", "text_run_render");

    // ── 0. Defensive: missing shape (source-pass only emits when set). ──
    if (!m_shape) {
        return NodeExecResult{
            ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true)};
    }

    if (m_cache_policy.reusable_across_frames() && m_cached_result &&
        !ctx.frame_input.has_camera_2_5d) {
        return NodeExecResult{ctx.acquire_owned_fb(*m_cached_result)};
    }

    // ── 1. Acquire the producer surface. Projected TextRuns render into
    // their tight local surface; the following TransformNode owns camera
    // projection and expands it into the canvas. Non-projected text keeps
    // the historical full-canvas source contract.
    const bool tight_surface =
        m_placement.tight_surface &&
        m_placement.surface_size.x > 0.0f &&
        m_placement.surface_size.y > 0.0f;
    const auto producer_surface = tight_surface
        ? detail::resolve_local_producer_surface(
              ctx.frame_input.width, ctx.frame_input.height,
              detail::ProducerSurfaceKind::Text,
              m_placement.surface_origin, m_placement.surface_size)
        : detail::ProducerSurfaceBounds{
              raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height}, false};
    const int surface_width = producer_surface.width();
    const int surface_height = producer_surface.height();
    detail::record_producer_surface(
        ctx.node_exec.counters,
        detail::ProducerSurfaceKind::Text,
        producer_surface,
        ctx.frame_input.width,
        ctx.frame_input.height);
    // A projected TextRun is cached as a tight local raster. The general
    // pool's best-fit reuse can hand it a full-frame physical allocation and
    // resize only the logical view, making cache weight and RSS lie about
    // the actual tight surface. Keep this allocation exact and disposable.
    auto fb = tight_surface && ctx.services.framebuffer_pool
        ? ctx.services.framebuffer_pool->acquire_owned_exact(
              surface_width, surface_height, /*clear=*/true)
        : ctx.acquire_owned_fb(surface_width, surface_height, /*clear=*/true);

    // ── 2. Validate pre-dispatch invariants. ──
    auto* backend = ctx.services.backend;
    // A direct caller may execute the same node again with the frame error
    // already latched.  Preserve the first diagnostic and avoid emitting a
    // duplicate log (the executor treats the frame as failed already).
    if (ctx.frame_error && ctx.frame_error->has_value()) {
        return NodeExecResult{ctx.frame_error->value()};
    }
    if (auto err = text_run::validate_execution(backend, m_name)) {
        if (ctx.frame_error && !ctx.frame_error->has_value()) {
            *ctx.frame_error = *err;
        }
        return NodeExecResult{std::move(*err)};
    }

    // ── 3-4. Unified TextRun rendering (shape prep + matrix + draw). ──
    // When diagnostic_overlay_only is true, skip text rendering entirely —
    // the framebuffer stays transparent so only the debug overlay markers
    // are visible.  Useful for comparing overlay-on vs overlay-off.
    std::size_t items_drawn = 0;
    f32 opacity = m_opacity_override.value_or(m_render_ref.world_transform.opacity);
    const Mat4 world_matrix = text_run::build_world_matrix(ctx, m_placement);
    if (!ctx.policy.diagnostic_overlay_only) {
#ifdef CHRONON3D_ENABLE_TEXT
        auto dispatch = text_run::render_text_run_item(
            ctx, *backend, *fb, *m_shape, m_placement, opacity);

        if (!dispatch) {

            text_run::report_failure(
                m_name,
                dispatch.error().code,
                render_backend_error_code_name(dispatch.error().code),
                dispatch.error().message);
            NodeExecutionError error{
                dispatch.error().code,
                m_name,
                std::string(dispatch.error().message)
            };
            if (ctx.frame_error && !ctx.frame_error->has_value()) {
                *ctx.frame_error = error;
            }
            return NodeExecResult{std::move(error)};
        }

        items_drawn = dispatch.value().items_drawn;
        if (dispatch.value().actual_ink_bbox) {
            ctx.node_exec.actual_ink_bbox = *dispatch.value().actual_ink_bbox;
        }
#else
        NodeExecutionError error{
            RenderBackendErrorCode::UnsupportedCapability,
            m_name,
            "text rendering is disabled in this build"
        };
        if (ctx.frame_error && !ctx.frame_error->has_value()) {
            *ctx.frame_error = error;
        }
        return NodeExecResult{std::move(error)};
#endif

        // ── 5. Per-frame debug diagnostic (opt-in via ctx.policy.diagnostics_enabled). ──
        if (ctx.policy.diagnostics_enabled) {
            text_run::report_diagnostic(
                m_name, *m_shape, items_drawn, opacity, world_matrix,
#ifdef CHRONON3D_ENABLE_TEXT
                chronon3d::hash_text_run_shape(
                    *m_shape, ctx.frame_input.sample_time.integral_frame())
#else
                std::nullopt
#endif
            );
        }
    } // diagnostic_overlay_only

    // NOTE: draw_text_run() already increments text_glyphs_rasterized
    // inside the processor.  Do NOT double-count here — the processor
    // is the single source of truth for telemetry.

    // TICKET-SIMPLICITY-VISIBILITY-CONTRACT — F1.E post-render visibility
    // audit.  Verifies the 6 invariants: font_resolved, shaping_succeeded,
    // finite, predicted_contains_world, clip_contains_visible_ink, and
    // alpha_bbox non-empty.  Emits structured spdlog::warn diagnostics
    // via verify_text_visibility() (one-shot per invariant).
    //
    // Uses world_matrix (computed once above), predicted_bbox (recomputed
    // here in diagnostics mode only), and the rendered framebuffer.
    // clip_rect = predicted_r (the compositor uses predicted_bbox as
    // clip_rect for TextRun nodes — see compute_dirty_clip in
    // tile_pruning.cpp).
    // Gated on CHRONON3D_BUILD_DIAGNOSTICS — zero overhead in production.
    // Skip in overlay-only mode: the framebuffer is transparent so there
    // is no rendered content to audit.
#if defined(CHRONON3D_BUILD_DIAGNOSTICS) && defined(CHRONON3D_ENABLE_TEXT)
    if (m_shape && fb && !ctx.policy.diagnostic_overlay_only) {
        auto pred = predicted_bbox(ctx, {});
        Rect predicted_r{};
        if (pred) {
            predicted_r = Rect{
                {static_cast<float>(pred->x0), static_cast<float>(pred->y0)},
                {static_cast<float>(pred->x1 - pred->x0),
                 static_cast<float>(pred->y1 - pred->y0)}};
        } else {
            predicted_r = Rect{
                {0, 0},
                {static_cast<float>(ctx.frame_input.width),
                 static_cast<float>(ctx.frame_input.height)}};
        }
        // clip_rect = predicted_r: matches compositor behavior for TextRun
        Rect clip_r = predicted_r;
        const Rect local_ink_bbox = local_bounds_to_rect(
            renderer::compute_text_run_visual_bounds(*m_shape));
        (void)local_ink_bbox;  // audit now reads it internally; see
                                // TICKET-VISIBILITY-OVERRIDE-DEDUP.
        const auto audit = verify_text_visibility(
            *m_shape, world_matrix, predicted_r, clip_r,
            fb.get(), m_name.c_str(),
            /*deduper=*/s_warn_deduper);

        // Emit structured geometric diagnostics (layout/ink/effect bounds,
        // baseline, anchor point, canvas position) when diagnostics are on.
        // The fields are already computed by the canonical audit; this simply
        // records them in the diagnostic log.
        if (ctx.policy.diagnostics_enabled) {
            text_run::report_geometry_diagnostic(m_name, audit);
        }
    }
#endif

    // ── 6. Text layout debug overlay + structured log (§5 + §6). ──
#ifdef CHRONON3D_ENABLE_TEXT
    if (ctx.policy.text_layout_debug) {
        const Mat4 world_matrix = text_run::build_world_matrix(ctx, m_placement);
        const bool use_local = ctx.policy.modular_coordinates;
        text_run::draw_text_debug_overlay(
            *fb, *m_shape, m_name, world_matrix, opacity,
            items_drawn, use_local,
            m_render_ref.world_transform.position);
    }
#endif
    if (m_cache_policy.reusable_across_frames() && fb &&
        !ctx.frame_input.has_camera_2_5d &&
        fb->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        // Native handles are batch/frame scoped. Never retain one in the
        // node cache: a later cache hit would resurrect a released Vulkan
        // surface and silently drop the text layer from the next frame.
        m_cached_result = std::make_shared<Framebuffer>(*fb);
    }

    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
