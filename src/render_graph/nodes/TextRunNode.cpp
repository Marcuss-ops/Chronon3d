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
#include <chronon3d/text/text_run_geometry.hpp>

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
#include "text_run/text_run_transform.hpp"
#include "text_run/text_run_diagnostics.hpp"
#ifdef CHRONON3D_ENABLE_TEXT
#include "text_run/text_run_debug_overlay.hpp"
#endif
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
// TICKET-FIX-ALPHA-SCANNER-DUP-V1 — private include for per-session
// TextBboxReporter dereference. Sibling-relative path (matches the
// text_run/* sub-directory include convention used elsewhere in this
// file: `text_run/text_run_execution.hpp` etc.).  Lives in
// `src/render_graph/executor/text_bbox_reporter.hpp` (NOT in
// `include/chronon3d/`); matches the AssetResolver + TextBboxReporter
// forward-decl pattern in the public SDK header NodeExecutionContext.
#include "../executor/text_bbox_reporter.hpp"
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

TextRunNode::TextRunNode(
    std::string name,
    std::shared_ptr<TextRunShape> shape,
    const ::chronon3d::RenderNode& render_ref,
    const cache::NodeCacheKey& key,
    TextRunPlacement placement,
    std::optional<f32> opacity_override,
    RenderNodeCachePolicy policy
)
    : RenderGraphNode(policy),
      m_name(std::move(name)),
      m_shape(std::move(shape)),
      m_render_ref(render_ref),
      m_key(key),
      m_placement(placement),
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

Mat4 TextRunNode::world_matrix(const RenderGraphContext& ctx) const {
    return text_run::build_world_matrix(ctx, m_placement);
}

std::optional<raster::BBox> TextRunNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) const {
    if (!m_shape) {
        return std::nullopt;
    }

    const Mat4 matrix = text_run::build_world_matrix(ctx, m_placement);

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

    // TICKET-TEXT-CLIP-PREDICTED-BBOX — contract violation guard (pre-clip).
    // The world matrix produced degenerate coordinates (e.g. the 403-px
    // residual offset documented in the ticket).  Conservative fallback
    // returns full canvas — same strategy as text_layout_debug below.
    // Each violation increments counters.text_bbox_contract_violations
    // (atomic, anti-false-share) so /api/runs diagnostics flags it.
    // NOTE: this guard runs *before* clip_to so a legitimately-off-canvas
    // layer (post-clip is_empty) hits the legacy BBox{0,0,0,0} cull path
    // below rather than the conservative fallback.
    const bool pre_clip_violation =
        !std::isfinite(bbox.x0) || !std::isfinite(bbox.y0) ||
        !std::isfinite(bbox.x1) || !std::isfinite(bbox.y1) ||
        bbox.is_empty();
    if (pre_clip_violation) {
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (ctx.policy.diagnostics_enabled) {
            spdlog::debug("[text-bbox] CONTRACT_VIOLATION node={} bbox=({}, {}, {}, {})",
                          m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1);
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    // TICKET-TEXT-CLIP-PREDICTED-BBOX — diagnostic logging.
    // Gated on diagnostics_enabled to avoid per-frame overhead in
    // production.  Logs the raw world bbox, the matrix translation,
    // and the placement matrix so the clipping root-cause can be
    // traced by comparing predicted_bbox vs actual alpha_bbox.
    if (ctx.policy.diagnostics_enabled) {
        const f32 mtx = matrix[3][0];
        const f32 mty = matrix[3][1];
        spdlog::debug(
            "[text-bbox] node={} "
            "world=({}, {}, {}, {}) "
            "w={} h={} "
            "matrix_tx={:.1f} matrix_ty={:.1f} "
            "spread={:.1f} "
            "canvas={}x{} "
            "placement[3][0]={:.1f} placement[3][1]={:.1f} "
            "op_override={}",
            m_name,
            bbox.x0, bbox.y0, bbox.x1, bbox.y1,
            bbox.x1 - bbox.x0, bbox.y1 - bbox.y0,
            mtx, mty,
            spread,
            ctx.frame_input.width, ctx.frame_input.height,
            m_placement.matrix[3][0], m_placement.matrix[3][1],
            m_opacity_override.has_value());
    }

    // When text_layout_debug is enabled, the overlay draws markers at
    // canvas center, layer origin, and alpha centroid — all outside the
    // tight text bbox.  Return full-canvas so the compositor doesn't clip
    // the overlay markers.
    if (ctx.policy.text_layout_debug) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    if (!ctx.policy.diagnostics_enabled) {
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
    }
    if (bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};  // legitimate post-clip cull
    }

    // TICKET-SIMPLICITY-CONSERVATIVE-BBOX — F1.C conservative fallback.
    // If predicted_bbox is suspiciously thin (height < 30% of font_size),
    // the world bbox computation likely under-estimated the glyph ink
    // extent.  This catches the class of bugs exemplified by the 19px-
    // sliver regression (TICKET-TEXT-CLIP-ASCENT) where a tight
    // predicted_bbox causes tile pruning to skip tiles that actually
    // contain visible text.  The font-size-proportional threshold avoids
    // false positives for small text on large canvases (e.g. 36pt caption
    // on 3840×2160).
    //
    // The primary defense is the post-render alpha_bbox expansion in
    // node_runner.cpp — this pre-render check is a safety net for cases
    // where the bbox is degenerate-thin before rendering even starts.
    if (m_shape) {
        const int bbox_h = std::max(0, bbox.y1 - bbox.y0);
        const int bbox_w = std::max(0, bbox.x1 - bbox.x0);
        // Read the canonical font_size from the shared layout (per
        // include/chronon3d/text/text_run_layout.hpp:104). The layout is
        // the SSOT for the per-run font_size; using it here makes the
        // conservative-bbox threshold proportional to the actual text
        // size (e.g. 88pt typewriters get 26px min-h, 16pt captions get
        // 5px min-h) instead of the previous constant 32.0f fallback that
        // mis-thresholded large and small text. Falls back to 32.0f for
        // degenerate shapes where layout is null or font_size is unset
        // (e.g. test fixtures, empty placeholders).
        const float font_size =
            (m_shape->layout && m_shape->layout->font_size > 0.0f)
                ? m_shape->layout->font_size
                : 32.0f;
        const int min_h = static_cast<int>(font_size * 0.3f);
        const int min_w = static_cast<int>(font_size * 0.5f);
        const bool suspiciously_thin =
            (bbox_h < min_h) || (bbox_w < min_w);
        if (suspiciously_thin) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                    1, std::memory_order_relaxed);
            }
            // TICKET-FIX-ALPHA-SCANNER-DUP-V1 — per-session warn-once
            // delegated to the canonical TU-local helper
            // `text_bbox_warn_once(...)` (declared in the anonymous
            // namespace at the top of this TU; Cat-3 anti-dup
            // consolidated: dedup logic declared ONCE).  No
            // `static bool` rot; no data race on parallel render; no
            // first-error-mask later invocations.  No string
            // duplication: the message format string is declared ONCE
            // at THIS call site (the grep-discoverable anchor); only
            // the dedup logic is shared via the helper.
            text_bbox_warn_once(
                ctx.node_exec.text_bbox_reporter,
                "[text-bbox] CONSERVATIVE_EXPAND node={} predicted=({}, {}, {}, {}) "
                "w={} h={} min=({}, {}) font_size={:.0f}",
                m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1,
                bbox_w, bbox_h, min_w, min_h, font_size);
            const int canvas_w = ctx.frame_input.width;
            const int canvas_h = ctx.frame_input.height;
            return raster::BBox{0, 0, canvas_w, canvas_h};
        }
    }

    // TICKET-SIMPLICITY-VISIBILITY-CONTRACT — §9 FU04 pre-render audit.
    // Calls `audit_text_visibility()` with the current tight bbox (no
    // framebuffer yet — alpha-bbox invariants deferred). If the audit
    // detects a `predicted_contains_world` violation (the math-side
    // contract fault that the FU01/FU03 guards don't catch), apply the
    // §9 violation response: substitute the `expanded_predicted_bbox`
    // (world_ink_bbox padded by `effect_padding` on all 4 sides) for
    // the original tight bbox. Logged via spdlog::warn + counter bump
    // so the telemetry surface surfaces the FU04 catch.
    //
    // Gated on CHRONON3D_BUILD_DIAGNOSTICS — in production builds the
    // tight bbox is returned unchanged (zero overhead). The expansion
    // is only computed in debug/diagnostic/inspect builds where the
    // audit is active.
    //
    // For TextRun nodes tile_pruning is already off by design (see
    // tile_pruning::compute_dirty_clip), so the "should_disable_tile_pruning"
    // flag is realized via the bbox expansion that guarantees
    // rasterization of all visible ink (the conceptual "disable" is the
    // guarantee that the tight bbox no longer under-estimates ink).
#if defined(CHRONON3D_BUILD_DIAGNOSTICS) && defined(CHRONON3D_ENABLE_TEXT)
    if (m_shape) {
        const Rect predicted_r{
            {static_cast<float>(bbox.x0), static_cast<float>(bbox.y0)},
            {static_cast<float>(bbox.x1 - bbox.x0),
             static_cast<float>(bbox.y1 - bbox.y0)}
        };
        const Rect local_ink_bbox = local_bounds_to_rect(
            renderer::compute_text_run_visual_bounds(*m_shape));
        (void)local_ink_bbox;  // post Step 2 fix (d), audit populates
                                // local_ink_bbox internally; the unused
                                // local is kept as belt-and-braces for
                                // future FU04 / FU11 work that re-reads
                                // the canonical accumulator result.
        const auto audit = audit_text_visibility(
            *m_shape, matrix, predicted_r, predicted_r,
            nullptr, spread);
        if (audit.should_disable_tile_pruning) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const auto& exp = audit.expanded_predicted_bbox;
            // TICKET-FIX-ALPHA-SCANNER-DUP-V1 — per-session warn-once
            // delegated to the canonical TU-local helper
            // `text_bbox_warn_once(...)` (declared in the anonymous
            // namespace at the top of this TU; one `warned` flag per
            // session is correct semantics since CONSERVATIVE_EXPAND
            // and FU04_EXPAND are mutually exclusive per-frame).
            // Cat-3 anti-dup consolidated: dedup logic shared via the
            // helper, message format string declared ONCE at THIS
            // call site (the grep-discoverable anchor).
            text_bbox_warn_once(
                ctx.node_exec.text_bbox_reporter,
                "[text-bbox] FU04_EXPAND node={} tight=({}, {}, {}, {}) "
                "expanded=({}, {}, {}, {}) effect_padding={:.1f}",
                m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1,
                exp.origin.x, exp.origin.y,
                exp.origin.x + exp.size.x, exp.origin.y + exp.size.y,
                spread);
            return raster::BBox{
                static_cast<int>(exp.origin.x),
                static_cast<int>(exp.origin.y),
                static_cast<int>(exp.origin.x + exp.size.x),
                static_cast<int>(exp.origin.y + exp.size.y)
            };
        }
    }
#endif

    return bbox;
}

// =============================================================================
// cache_key
// =============================================================================

cache::NodeCacheKey TextRunNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // TICKET-122: use current evaluation frame for cache isolation.
    key.frame = ctx.frame_input.frame;

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
    key.params_hash = hash_combine(
        key.params_hash,
        hash_bytes(&m_placement.matrix[0][0], sizeof(Mat4)));
    if (m_opacity_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }

    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B).
    // Conditional on `has_camera_2_5d` globally.  Fold zoom + position.z +
    // parent + DOF into `params_hash` so zoom-animated frames produce
    // distinct per-frame keys.
    if (ctx.frame_input.has_camera_2_5d) {
        cache::fold_camera_into_params_hash(key, ctx.frame_input.camera_2_5d);
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

    // ── 3-4. Unified TextRun rendering (shape prep + matrix + draw). ──
    // When diagnostic_overlay_only is true, skip text rendering entirely —
    // the framebuffer stays transparent so only the debug overlay markers
    // are visible.  Useful for comparing overlay-on vs overlay-off.
    std::size_t items_drawn = 0;
    f32 opacity = m_opacity_override.value_or(m_render_ref.world_transform.opacity);
    const Mat4 world_matrix = text_run::build_world_matrix(ctx, m_placement);
    if (!ctx.policy.diagnostic_overlay_only) {
        auto dispatch = text_run::render_text_run_item(
            ctx, *backend, *fb, *m_shape, m_placement, opacity);

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

        items_drawn = dispatch.value().items_drawn;
        if (dispatch.value().actual_ink_bbox) {
            ctx.node_exec.actual_ink_bbox = *dispatch.value().actual_ink_bbox;
        }

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
        verify_text_visibility(
            *m_shape, world_matrix, predicted_r, clip_r,
            fb.get(), m_name.c_str(),
            /*deduper=*/s_warn_deduper);
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

    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
