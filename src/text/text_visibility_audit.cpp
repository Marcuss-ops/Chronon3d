// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_visibility_audit.cpp — FU02 implementation (Step 9 §A slimmed)
//
// Step 9 §A — domain decomposition per user spec verbatim:
//   - alpha-bbox scan → `src/text/alpha_bbox_scanner.{hpp,cpp}` (UNgated,
//     reusable from inspect-text, pipeline parity, glow acceptance, video
//     acceptance, and golden test C++/Python consumers without dragging
//     in the diagnostic gating macro). See alpha_bbox_scanner.hpp for
//     the canonical pixel-walk implementation.
//   - reporting (verify_text_visibility + WarnOnceDeduper + TextWarningKind)
//     → `src/text/text_visibility_reporting.{hpp,cpp}` (DIAGNOSTICS-gated).
//   - this file retains ONLY: font, shaping, bbox (local/world), containment,
//     and status computation. Pure data module — no spdlog dependency, no
//     warn-once state, no hash-table storage.
//
// Step 2 (FA §10 / TICKET-TEXT-VISIBILITY-CONTRACT) deltas (preserved):
//   (b) `clip_contains_visible_ink` uses containment invariant
//       `expand(clip, kTextAuditBBoxTolerance) ⊇ rendered_alpha_bbox` instead
//       of the prior `rect_intersects` (closes TICKET-TEXT-CLIP-19-PIXEL-SLIVER).
//   The early-exit fix (a) is now in `alpha_bbox_scanner.cpp`.
//   The WarnOnceDeduper fix (c) is now in `text_visibility_reporting.cpp`.
//
// GATING: entire body is gated by `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`.
// When OFF (production SDK build): the file pre-processes to a no-op TU with
// zero symbols, no link impact, and full Cat-3 / Cat-5 compliance.
// When ON: exports the canonical `audit_text_visibility()` pure function.
//
// Anti-duplication:
//   - Local `rect_*` helpers + `transform_aabb` + `expand_rect` live in an
//     anonymous namespace inside this TU so they do not leak to the
//     textual ABI. The library → tests include dependency is forbidden
//     per AGENTS.md Cat-3 / Cat-5.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/alpha_bbox_scanner.hpp>   // canonical alpha-bbox scan (Step 9 §A)

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

#include <algorithm>                          // std::min, std::max
#include <cmath>                              // std::isfinite
#include <cstring>                            // std::strlen
#include <limits>                             // std::numeric_limits
#include <string_view>                        // std::hash<std::string_view>

#include <chronon3d/text/text_run_shape.hpp>  // full TextRunShape definition
#include <chronon3d/text/text_run_geometry.hpp>  // canonical
                                               // compute_text_run_visual_bounds
                                               // (the SINGLE source of truth
                                               // for per-glyph TRS bbox math
                                               // — anti-duplication: the audit
                                               // does NOT re-implement the
                                               // per-glyph math; it delegates
                                               // to the canonical helper).
#include <chronon3d/core/memory/framebuffer.hpp>  // Framebuffer::width/height/pixel

#include <spdlog/spdlog.h>                       // structured diagnostics

#include <glm/glm.hpp>                       // glm::mat4, glm::vec4

namespace chronon3d {

namespace {

// ── Rect helpers — file-local (TU-private) ───────────────────────────────
// These helpers are kept in an anonymous namespace inside this TU so they
// do not leak to the textual ABI.

bool rect_is_finite(const Rect& r) noexcept {
    return std::isfinite(r.origin.x) && std::isfinite(r.origin.y)
        && std::isfinite(r.size.x)  && std::isfinite(r.size.y);
}

bool rect_contains_tol(const Rect& outer,
                       const Rect& inner,
                       float tol) noexcept {
    const float ox0 = outer.origin.x - tol;
    const float oy0 = outer.origin.y - tol;
    const float ox1 = outer.origin.x + outer.size.x + tol;
    const float oy1 = outer.origin.y + outer.size.y + tol;
    return (inner.origin.x >= ox0) && (inner.origin.y >= oy0)
        && (inner.origin.x + inner.size.x <= ox1)
        && (inner.origin.y + inner.size.y <= oy1);
}

bool rect_intersects(const Rect& a, const Rect& b) noexcept {
    return (a.origin.x + a.size.x > b.origin.x)
        && (b.origin.x + b.size.x > a.origin.x)
        && (a.origin.y + a.size.y > b.origin.y)
        && (b.origin.y + b.size.y > a.origin.y);
}

// `rect_uses_containment()` — canonical alias used by the audit to keep
// the call-site semantics labelled. Equivalent to `rect_contains_tol`
// (which is the FU04 invariant primitive); the alias names the *audit*
// intent rather than the math.
bool rect_uses_containment(const Rect& outer_clip,
                           const Rect& inner_alpha,
                           float tol) noexcept {
    return rect_contains_tol(outer_clip, inner_alpha, tol);
}

Rect transform_aabb(const Rect& local, const Mat4& M) noexcept {
    const float lx0 = local.origin.x;
    const float ly0 = local.origin.y;
    const float lx1 = local.origin.x + local.size.x;
    const float ly1 = local.origin.y + local.size.y;
    float xmin = +std::numeric_limits<float>::infinity();
    float ymin = +std::numeric_limits<float>::infinity();
    float xmax = -std::numeric_limits<float>::infinity();
    float ymax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 4; ++i) {
        const float lx = (i & 1) ? lx1 : lx0;
        const float ly = (i & 2) ? ly1 : ly0;
        // We only carry x and y into world space; z is the text frame depth
        // (TICKET-TEXT-CLIP-ASCENT confirmed not relevant for the 2.5D AABB
        // case). Homogeneous w=1 preserves the projective nature of
        // Mat4 for camera-projected transforms.
        const Vec4  w  = M * Vec4(lx, ly, 0.0f, 1.0f);
        xmin = std::min(xmin, w.x);  ymin = std::min(ymin, w.y);
        xmax = std::max(xmax, w.x);  ymax = std::max(ymax, w.y);
    }
    return Rect{ {xmin, ymin}, {xmax - xmin, ymax - ymin} };
}

// `expand_rect()` — §9 FU04 violation response helper. Returns `r` padded
// by `padding` on all 4 sides. AABB semantics, axis-aligned. If `padding`
// is negative, the function still produces a valid rect (potentially
// degenerate). If `r` has non-finite coordinates, the result is undefined
// (caller is responsible for guarding via `rect_is_finite()` first).
Rect expand_rect(const Rect& r, float padding) noexcept {
    return Rect{
        {r.origin.x - padding, r.origin.y - padding},
        {r.size.x + 2.0f * padding, r.size.y + 2.0f * padding}
    };
}



} // namespace (TU-private helpers)

TextVisibilityAudit audit_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    float               effect_padding) noexcept
{
    TextVisibilityAudit audit{};

    // ── font + shaping stage ─────────────────────────────────────────
    audit.font_resolved        = (shape.engine != nullptr);
    audit.shaping_succeeded    = (shape.layout && !shape.layout->placed.glyphs.empty());
    audit.glyph_count          = shape.layout ? shape.layout->placed.glyphs.size() : 0;

    // ── local_ink_bbox (canonical per-glyph TRS math) ─────────────
    // Delegate to the canonical `compute_text_run_visual_bounds()` from
    // `src/text/text_run_geometry.cpp` (the SINGLE source of truth for
    // per-glyph TRS bbox math). The function walks both active and
    // crossfade glyph vectors + accounts for ascent/descent, real
    // per-glyph advance, 2.5D depth scale, blur/stroke padding, and the
    // TICKET-TEXT-CLIP-ASCENT ink-floor fix. Returns std::nullopt when
    // the shape is empty (no glyphs) — we fall back to zero-rect in that
    // case to preserve the legacy "point at world origin" semantics for
    // unit tests that pass empty shapes.
    if (auto local = renderer::compute_text_run_visual_bounds(shape)) {
        audit.local_ink_bbox = Rect{
            {local->min_x, local->min_y},
            {local->max_x - local->min_x, local->max_y - local->min_y}
        };
    } else {
        audit.local_ink_bbox = Rect{};  // empty shape fallback
    }

    // ── world_ink_bbox (transform pipeline) ───────────────────
    audit.world_ink_bbox       = transform_aabb(audit.local_ink_bbox, world_matrix);

    // ── caller-provided bboxes ─────────────────────────────────
    audit.predicted_bbox       = predicted_bbox;
    audit.clip_rect            = clip_rect;

    // ── invariants ─────────────────────────────────────────
    audit.finite               = rect_is_finite(audit.local_ink_bbox)
                                && rect_is_finite(audit.world_ink_bbox)
                                && rect_is_finite(predicted_bbox)
                                && rect_is_finite(clip_rect);
    audit.predicted_contains_world = rect_contains_tol(
        predicted_bbox,
        audit.world_ink_bbox,
        kTextAuditBBoxTolerance);

    // ── alpha-bbox (only when framebuffer provided) ──────────
    const bool framebuffer_supplied =
        (rendered_output != nullptr
         && rendered_output->width() > 0
         && rendered_output->height() > 0);
    if (framebuffer_supplied) {
        audit.rendered_alpha_bbox = alpha_bbox_scan(*rendered_output);  // canonical scanner (Step 9 §A)
        // Step 2 fix (b): CONTAINMENT check (with `kTextAuditBBoxTolerance`
        // sub-pixel slack) instead of intersection. Closes the
        // TICKET-TEXT-CLIP-19-PIXEL-SLIVER regression: a clip that keeps
        // only 1 pixel of ink used to pass `rect_intersects`; only true
        // containment earns `clip_contains_visible_ink == true`.
        if (!rect_is_finite(audit.rendered_alpha_bbox)) {
            audit.clip_contains_visible_ink = false;
        } else {
            audit.clip_contains_visible_ink =
                rect_uses_containment(clip_rect, audit.rendered_alpha_bbox,
                                     kTextAuditBBoxTolerance);
        }
    } else {
        // No framebuffer provided → alpha-bbox NOT measured. The contract
        // sets `clip_contains_visible_ink = false` as the safe default so
        // callers honouring it MUST inspect `rendered_alpha_bbox` (still
        // zero-rect) before consuming this field. Used by tile-pruning and
        // the cross-pipeline parity tests (FU08) where the framebuffer is
        // not in scope — those callers check the math invariants
        // (`finite`, `predicted_contains_world`) and explicitly skip the
        // pixel-side invariant (false-by-default for unscanned).
        audit.rendered_alpha_bbox  = Rect{};
        audit.clip_contains_visible_ink = false;
    }

    // §9 FU04 — status cascade + violation response
    // Status computation: PASS iff all 4 critical invariants hold AND
    // (no framebuffer OR clip_contains_visible_ink). FAIL otherwise.
    const bool critical_pass = audit.font_resolved
                            && audit.shaping_succeeded
                            && audit.finite
                            && audit.predicted_contains_world;
    if (!critical_pass) {
        audit.status = TextVisibilityStatus::FAIL;
    } else if (framebuffer_supplied && !audit.clip_contains_visible_ink) {
        audit.status = TextVisibilityStatus::FAIL;
    } else {
        audit.status = TextVisibilityStatus::PASS;
    }
    // Violation response: set the flag + compute the expansion. Triggered
    // iff `predicted_contains_world` is false (the math-side contract
    // violation that the user-facing status would also flag). The
    // expansion is `world_ink_bbox` padded by `effect_padding` on all
    // 4 sides. The caller (TextRunNode::predicted_bbox) reads the flag
    // + the expansion and substitutes it for the original tight bbox.
    audit.should_disable_tile_pruning = !audit.predicted_contains_world;
    if (audit.should_disable_tile_pruning
        && rect_is_finite(audit.world_ink_bbox)) {
        audit.expanded_predicted_bbox =
            expand_rect(audit.world_ink_bbox, effect_padding);
    }

    return audit;
}


} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
