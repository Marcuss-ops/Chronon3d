// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_visibility_reporting.cpp — Step 9 §A implementation
//
// Side-effecting half of the text-visibility contract:
//   - `make_warning_key()`  — canonical helper that builds a `TextWarningKey`
//                             from a node_name + a `TextWarningKind`.
//   - `verify_text_visibility()` — calls `audit_text_visibility()` then
//                             emits 6 `spdlog::warn` diagnostics, dedup'd
//                             per `(node_id, kind)` via `WarnOnceDeduper`.
//
// Extracted from `text_visibility_audit.cpp` so the audit file is now a
// pure-data module with no spdlog dependency, no warn-once state, no
// hash-table storage — and can be re-used (e.g. by the CLI inspect-text
// command) without dragging in the spdlog compile cost.
//
// GATING: entire body is gated by `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`,
// matching the audit header. When diagnostics=OFF, this TU pre-processes
// to empty (zero symbols, zero link impact, full Cat-3 zero-surface
// compliance).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_visibility_reporting.hpp>
#include <chronon3d/text/text_visibility_audit.hpp>   // full TextVisibilityAudit def

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

#include <cstring>                            // std::strlen
#include <string_view>                        // std::hash<std::string_view>

#include <spdlog/spdlog.h>                    // structured diagnostics

namespace chronon3d {

namespace {

// `make_warning_key()` — canonical helper that builds a `TextWarningKey`
// from a node_name + a `TextWarningKind`. node_name is hashed via
// `std::hash<std::string_view>` so the deduper key is stable across calls.
// Unnamed nodes (null OR empty string) collapse to a sentinel
// `node_id = -1` so they share one slot per kind (the canonical "any
// unnamed node warns once" pattern).
//
// Fold-to-int: 2^31 distinct names hash to distinct ids; birthday collision
// ~46k. The deduper is a diagnostic aid, not a security boundary.
TextWarningKey make_warning_key(const char* node_name,
                                TextWarningKind kind) noexcept {
    int id;
    if (!node_name || !*node_name) {
        id = -1;
    } else {
        const std::size_t h = std::hash<std::string_view>{}(
            std::string_view(node_name,
                             std::strlen(node_name)));
        id = static_cast<int>(h & 0x7fffffff);
    }
    return TextWarningKey{id, kind};
}

} // namespace (TU-private helpers)

TextVisibilityAudit verify_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    const char*         node_name,
    WarnOnceDeduper&    deduper,
    float               effect_padding
) {
    const auto audit = audit_text_visibility(
        shape, world_matrix, predicted_bbox, clip_rect, rendered_output,
        effect_padding);

    // ── F1.E — 6 invariants with per-scope warn-once dedup ────────
    // Replaces the prior 6 file-scope `static bool` one-shots
    // (process-wide state, data race in parallel render, first-error
    // masked later per-invocation diagnostics). Each warning fires at
    // most once per `(node_id, TextWarningKind)` pair per `deduper`
    // lifetime — see `WarnOnceDeduper` doc for the per-instance scoping
    // contract (Cat-5 anti-singleton compliance).
    const char* nm = node_name ? node_name : "<unnamed>";

    if (!audit.font_resolved) {
        const TextWarningKey key = make_warning_key(nm, TextWarningKind::FontUnresolved);
        if (deduper.mark_warned(key)) {
            spdlog::warn("[text-vis] FONT_UNRESOLVED node={} engine=nullptr", nm);
        }
    }

    if (!audit.shaping_succeeded) {
        const TextWarningKey key = make_warning_key(nm, TextWarningKind::ShapingFailed);
        if (deduper.mark_warned(key)) {
            spdlog::warn("[text-vis] SHAPING_FAILED node={} glyph_count={}",
                         nm, audit.glyph_count);
        }
    }

    if (!audit.finite) {
        const TextWarningKey key = make_warning_key(nm, TextWarningKind::BboxNonFinite);
        if (deduper.mark_warned(key)) {
            spdlog::warn("[text-vis] BBOX_NON_FINITE node={}", nm);
        }
    }

    if (!audit.predicted_contains_world) {
        const TextWarningKey key = make_warning_key(nm, TextWarningKind::PredictedTooSmall);
        if (deduper.mark_warned(key)) {
            spdlog::warn("[text-vis] PREDICTED_TOO_SMALL node={}", nm);
        }
    }

    if (rendered_output && !audit.clip_contains_visible_ink) {
        const TextWarningKey key = make_warning_key(nm, TextWarningKind::ClipDropsInk);
        if (deduper.mark_warned(key)) {
            spdlog::warn("[text-vis] CLIP_DROPS_INK node={}", nm);
        }
    }

    if (rendered_output && audit.shaping_succeeded) {
        const bool alpha_empty =
            audit.rendered_alpha_bbox.size.x <= 0.0f ||
            audit.rendered_alpha_bbox.size.y <= 0.0f;
        if (alpha_empty) {
            const TextWarningKey key = make_warning_key(nm, TextWarningKind::NoInkRendered);
            if (deduper.mark_warned(key)) {
                spdlog::warn("[text-vis] NO_INK_RENDERED node={} glyph_count={}",
                             nm, audit.glyph_count);
            }
        }
    }

    return audit;
}

} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
