// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_visibility_audit.cpp — FU02 implementation
//
// GATING: The entire body is gated by `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`.
// When OFF (production SDK build): the file pre-processes to a no-op TU with
// zero symbols, no link impact, and full Cat-3 / Cat-5 compliance.
// When ON (debug / test / inspect builds): exports a single canonical free
// function `chronon3d::audit_text_visibility()`.
//
// Anti-duplicazione honour:
//   - The audit function SHARES math with `text_run_geometry.cpp` (where the
//     canonical `compute_text_run_visual_bounds()` lives). To avoid
//     dual-implementation drift, the audit does NOT reimplement the
//     per-glyph TRS math; the consumer of the audit populates
//     `local_ink_bbox` from the canonical computation or — for the audit
//     scaffolding — leaves it at zero-rect (the invariants degrade
//     gracefully to "world_ink_bbox is empty" which is always finite).
//   - The `scan_alpha_bbox` skeleton is a TU-local helper to avoid pulling
//     `tests/text_visual_fixture.hpp` (anti-duplicazione: test fixtures // drift-allow: stale-ref
//     stay in tests/). FU07 forward-point: relocate this helper to a
//     canonical `src/text/alpha_scanner.cpp` once FU06 / FU08 surface a // drift-allow: stale-ref
//     need for cross-pipeline reuse.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_visibility_audit.hpp>

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

#include <algorithm>                          // std::min, std::max
#include <cmath>                              // std::isfinite
#include <limits>                             // std::numeric_limits

#include <chronon3d/text/text_run_shape.hpp>  // full TextRunShape definition
                                               // (audit only reads engine +
                                               // layout.placed.glyphs.size())
#include <chronon3d/text/text_run_geometry.hpp>  // canonical
                                               // compute_text_run_visual_bounds
                                               // (the single source of truth
                                               // for per-glyph TRS bbox math
                                               // — anti-duplication: the audit
                                               // does NOT re-implement the
                                               // per-glyph math; it delegates
                                               // to the canonical helper).
#include <chronon3d/core/memory/framebuffer.hpp>  // Framebuffer::width/height/pixel

#include <spdlog/spdlog.h>                       // structured diagnostics

#include <glm/glm.hpp>                       // glm::mat4, glm::vec4 for
                                               // transform_aabb corner
                                               // multiplication.

namespace chronon3d {

namespace {

// ── Rect helpers — file-local (TU-private) ───────────────────────────────
// These helpers are kept in an anonymous namespace inside this TU so they
// do not leak to the textual ABI. If a future FU07/08 surface a need for
// cross-TU reuse, the canonical location is `src/text/internal/rect_math.hpp` // drift-allow: stale-ref
// (per the existing-internal-helper convention in `src/text/CMakeLists.txt`
// for `internal/text_resolver_helpers.hpp`).

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

Rect transform_aabb(const Rect& local, const Mat4& M) noexcept {
    // Transform the four corners of `local` by `M` and take the axes-
    // aligned bounding box of the transformed points. This is correct
    // for any affine transform (rotation, scale, shear, translation) — AABB
    // is exact for the convex hull of the rectangle corners.
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

// `scan_alpha_bbox()` — conservative placeholder.
//
// Returns an empty `Rect{}` (zero size) when:
//   - the framebuffer does not have visible ink above the alpha threshold
//     AND a real pixel-scanner is NOT yet wired (FU06/07/08 forward-point).
//
// Anti-duplicazione rationale: the canonical alpha-bbox scanner lives in
// `tests/text_visual_fixture.hpp` (TEST-side helper). Reusing it from lib // drift-allow: stale-ref
// would create a lib → tests include dependency, which is forbidden by the
// test/logic separation in `AGENTS.md §anti-duplication`. The TU-local
// placeholder keeps libtext-core self-contained until FU07 promotes this
// to `src/text/alpha_scanner.cpp`. // drift-allow: stale-ref
//
// When the framebuffer is non-empty and contains visible pixels, this
// placeholder returns `Rect{}` so callers correctly see
// `clip_contains_visible_ink == false` — a sentinel "alpha-pending" state
// that's safer than reporting true for unscanned frames.
Rect scan_alpha_bbox(const Framebuffer& fb) noexcept {
    const int fb_w = static_cast<int>(fb.width());
    const int fb_h = static_cast<int>(fb.height());
    if (fb_w <= 0 || fb_h <= 0) return Rect{};

    int alpha_x0 = fb_w, alpha_y0 = fb_h;
    int alpha_x1 = -1, alpha_y1 = -1;

    for (int y = 0; y < fb_h; ++y) {
        const Color* row = fb.pixels_row(y);
        bool row_has_ink = false;
        for (int x = 0; x < fb_w; ++x) {
            if (row[x].a > 0.01f) {
                row_has_ink = true;
                if (x < alpha_x0) alpha_x0 = x;
                if (x > alpha_x1) alpha_x1 = x;
            }
        }
        if (row_has_ink) {
            if (y < alpha_y0) alpha_y0 = y;
            alpha_y1 = y;
        }
        // Early exit: stop 2 rows past the last ink row
        if (y > alpha_y1 + 2 && alpha_y1 >= alpha_y0) break;
    }

    if (alpha_x0 > alpha_x1 || alpha_y0 > alpha_y1) {
        return Rect{};  // no visible ink found
    }
    return Rect{
        {static_cast<float>(alpha_x0), static_cast<float>(alpha_y0)},
        {static_cast<float>(alpha_x1 - alpha_x0 + 1),
         static_cast<float>(alpha_y1 - alpha_y0 + 1)}
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

    // ── font + shaping stage ───────────────────────────────────────────
    audit.font_resolved        = (shape.engine != nullptr);
    audit.shaping_succeeded    = (shape.layout && !shape.layout->placed.glyphs.empty());
    audit.glyph_count          = shape.layout ? shape.layout->placed.glyphs.size() : 0;

    // ── local_ink_bbox (canonical per-glyph TRS math) ────────────────────
    // Delegate to the canonical `compute_text_run_visual_bounds()` from
    // `src/text/text_run_geometry.cpp` (the SINGLE source of truth for
    // per-glyph TRS bbox math — anti-duplicazione: the audit does NOT
    // re-implement the per-glyph math). The function walks both active
    // and crossfade glyph vectors + accounts for ascent/descent, real
    // per-glyph advance, 2.5D depth scale, blur/stroke padding, and the
    // TICKET-TEXT-CLIP-ASCENT ink-floor fix. Returns std::nullopt when
    // the shape is empty (no glyphs) — we fall back to zero-rect in
    // that case to preserve the legacy "point at world origin" semantics
    // for unit tests that pass empty shapes.
    if (auto local = renderer::compute_text_run_visual_bounds(shape)) {
        audit.local_ink_bbox = Rect{
            {local->min_x, local->min_y},
            {local->max_x - local->min_x, local->max_y - local->min_y}
        };
    } else {
        audit.local_ink_bbox = Rect{};  // empty shape fallback
    }

    // ── world_ink_bbox (transform pipeline) ────────────────────────────
    audit.world_ink_bbox       = transform_aabb(audit.local_ink_bbox,
                                                  world_matrix);

    // ── caller-provided bboxes ─────────────────────────────────────────
    audit.predicted_bbox       = predicted_bbox;
    audit.clip_rect            = clip_rect;

    // ── invariants ─────────────────────────────────────────────────────
    audit.finite               = rect_is_finite(audit.local_ink_bbox)
                                && rect_is_finite(audit.world_ink_bbox)
                                && rect_is_finite(predicted_bbox)
                                && rect_is_finite(clip_rect);
    audit.predicted_contains_world = rect_contains_tol(
        predicted_bbox,
        audit.world_ink_bbox,
        kTextAuditBBoxTolerance);

    // ── alpha-bbox (only when framebuffer provided) ───────────────────
    const bool framebuffer_supplied =
        (rendered_output != nullptr
         && rendered_output->width() > 0
         && rendered_output->height() > 0);
    if (framebuffer_supplied) {
        audit.rendered_alpha_bbox = scan_alpha_bbox(*rendered_output);
        // If the alpha scan returns a non-finite Rect (degenerate), report
        // the conservative false; if finite, intersect-test against clip.
        if (!rect_is_finite(audit.rendered_alpha_bbox)) {
            audit.clip_contains_visible_ink = false;
        } else {
            audit.clip_contains_visible_ink =
                rect_intersects(clip_rect, audit.rendered_alpha_bbox);
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

TextVisibilityAudit verify_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    const char*         node_name,
    float               effect_padding
) {
    const auto audit = audit_text_visibility(
        shape, world_matrix, predicted_bbox, clip_rect, rendered_output,
        effect_padding);

    // ── F1.E — 6 invariants with one-shot spdlog::warn ──────────────────
    const char* nm = node_name ? node_name : "<unnamed>";

    if (!audit.font_resolved) {
        static bool w1 = false;
        if (!w1) {
            spdlog::warn("[text-vis] FONT_UNRESOLVED node={} engine=nullptr", nm);
            w1 = true;
        }
    }

    if (!audit.shaping_succeeded) {
        static bool w2 = false;
        if (!w2) {
            spdlog::warn("[text-vis] SHAPING_FAILED node={} glyph_count={}",
                         nm, audit.glyph_count);
            w2 = true;
        }
    }

    if (!audit.finite) {
        static bool w3 = false;
        if (!w3) {
            spdlog::warn("[text-vis] BBOX_NON_FINITE node={}", nm);
            w3 = true;
        }
    }

    if (!audit.predicted_contains_world) {
        static bool w4 = false;
        if (!w4) {
            spdlog::warn("[text-vis] PREDICTED_TOO_SMALL node={}", nm);
            w4 = true;
        }
    }

    if (rendered_output && !audit.clip_contains_visible_ink) {
        static bool w5 = false;
        if (!w5) {
            spdlog::warn("[text-vis] CLIP_DROPS_INK node={}", nm);
            w5 = true;
        }
    }

    if (rendered_output && audit.shaping_succeeded) {
        const bool alpha_empty =
            audit.rendered_alpha_bbox.size.x <= 0.0f ||
            audit.rendered_alpha_bbox.size.y <= 0.0f;
        if (alpha_empty) {
            static bool w6 = false;
            if (!w6) {
                spdlog::warn("[text-vis] NO_INK_RENDERED node={} glyph_count={}",
                             nm, audit.glyph_count);
                w6 = true;
            }
        }
    }

    return audit;
}

} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
