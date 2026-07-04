// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_font_resolver.cpp
//
// M1.5#8 — single-responsibility module: the FontResolver service.
//
// What lives here:
//   * `FontResolver::resolve(const FontRequest&)` — full fallback chain
//     (5-tier walking, identical to the pre-split `resolve_fallback_fonts`
//     logic in text_resolver.cpp).
//   * `resolve_fallback_fonts(FontSpec, FontEngine&)` — DEPRECATED
//     free-function form kept for back-compat.  Marked [[deprecated]] in
//     include/chronon3d/text/text_resolver.hpp.  The cpp body IS the
//     legacy chain body inlined for clarity, BUT the gate to enter the
//     chain is identical — there is ONE impl of the fallback logic
//     (this one).  No copy in the backend or in the builder.
//
// What does NOT live here:
//   * Span splitting — see text_span_resolver.cpp.
//   * Bidi segmentation — see text_bidi_resolver.cpp (calls
//     segment_bidi_runs from src/backends/text/bidi_segmenter.cpp).
//   * Orchestration — text_run_resolver.cpp.

#include "src/text/resolver/font_resolver.hpp"

#include "src/text/internal/text_resolver_helpers.hpp"

#include <algorithm>
#include <cstddef>

namespace chronon3d::text::resolver {

namespace {

// ── Canonical 5-tier fallback candidates (verbatim from prior impl) ──────
//
// Tier 0: primary font unchanged (caller already checked can_load above).
// Tier 1: same font_family, no explicit path (engine family→path mapping).
// Tier 2: sans-serif candidates (DejaVu Sans, Liberation Sans, Arial, Helvetica).
// Tier 3: serif candidates (DejaVu Serif, Liberation Serif, Times New Roman, Georgia).
// Tier 4: primary returned as-is (caller must treat as invisible).
//
// The extra-family candidates provided via FontRequest (if any) are tried
// BEFORE tier 4; their fallback_index is 3.
constexpr const char* kSansCandidates[] = {
    "DejaVu Sans",
    "Liberation Sans",
    "Arial",
    "Helvetica",
};
constexpr const char* kSerifCandidates[] = {
    "DejaVu Serif",
    "Liberation Serif",
    "Times New Roman",
    "Georgia",
};

[[nodiscard]] bool try_spec(FontEngine& engine, const FontSpec& spec) {
    return engine.can_load(spec);
}

[[nodiscard]] FontSpec build_family_only(const FontSpec& primary) {
    FontSpec family_only;
    family_only.font_family = primary.font_family;
    family_only.font_weight = primary.font_weight;
    family_only.font_style  = primary.font_style;
    family_only.font_size   = primary.font_size;
    return family_only;
}

[[nodiscard]] FontSpec build_named(const FontSpec& primary, const char* family) {
    FontSpec cand;
    cand.font_family = family;
    cand.font_weight = primary.font_weight;
    cand.font_style  = primary.font_style;
    cand.font_size   = primary.font_size;
    return cand;
}

} // namespace

// ── FontResolver::resolve — the canonical font fallback ───────────────────
FontResolutionResult FontResolver::resolve(const FontRequest& req) const {
    FontResolutionResult result;
    result.resolved = req.primary;

    // Canonicalize family + weight via the wired text_resolver_helpers
    // before the fallback chain runs (TICKET-101 follow-up carried).
    chronon3d::text::internal::apply_fontspec_canonicalization(result.resolved);

    // ── Tier 0: primary font ──────────────────────────────────────────
    if (try_spec(engine_, result.resolved)) {
        result.status = FontResolutionResult::Status::Loaded;
        result.fallback_index = -1;
        return result;
    }

    // ── Tier 1: same family, no explicit path ─────────────────────────
    if (req.prefer_family_coherent && !result.resolved.font_family.empty()) {
        FontSpec family_only = build_family_only(result.resolved);
        if (try_spec(engine_, family_only)) {
            result.resolved = family_only;
            result.status = FontResolutionResult::Status::FellBack;
            result.fallback_index = 0;
            return result;
        }
    }

    // ── Tier 2: platform-generic sans-serif fallback ─────────────────
    for (const auto* family : kSansCandidates) {
        FontSpec cand = build_named(result.resolved, family);
        if (try_spec(engine_, cand)) {
            result.resolved = cand;
            result.status = FontResolutionResult::Status::FellBack;
            result.fallback_index = 1;
            return result;
        }
    }

    // ── Tier 3: platform-generic serif fallback ───────────────────────
    for (const auto* family : kSerifCandidates) {
        FontSpec cand = build_named(result.resolved, family);
        if (try_spec(engine_, cand)) {
            result.resolved = cand;
            result.status = FontResolutionResult::Status::FellBack;
            result.fallback_index = 2;
            return result;
        }
    }

    // ── Tier 4: extra-family candidates (caller-supplied) ────────────
    if (req.extra_family_candidates && req.extra_family_candidates_count > 0) {
        for (std::size_t i = 0; i < req.extra_family_candidates_count; ++i) {
            const char* family = req.extra_family_candidates[i];
            FontSpec cand = build_named(result.resolved, family);
            if (try_spec(engine_, cand)) {
                result.resolved = cand;
                result.status = FontResolutionResult::Status::FellBack;
                result.fallback_index = 3;
                return result;
            }
        }
    }

    // ── Tier 5: return primary as-is (Unresolved) ────────────────────
    result.resolved = req.primary;
    result.status = FontResolutionResult::Status::Unresolved;
    result.fallback_index = -1;
    return result;
}

} // namespace chronon3d::text::resolver

// ═══════════════════════════════════════════════════════════════════════════
// resolve_fallback_fonts — DEPRECATED back-compat shim
// ═══════════════════════════════════════════════════════════════════════════
//
// Marked [[deprecated]] in include/chronon3d/text/text_resolver.hpp.
// Implementation delegated to FontResolver::resolve so the "un solo
// servizio" architectural constraint stays true — there is exactly one
// implementation of the fallback chain (text_font_resolver.cpp's
// resolve() above).  This free function is a thin adapter.

namespace chronon3d {

FontSpec resolve_fallback_fonts(
    FontSpec primary,
    FontEngine& engine
) {
    chronon3d::text::resolver::FontRequest req;
    req.primary = primary;
    auto res = chronon3d::text::resolver::FontResolver{engine}.resolve(req);
    return res.resolved;
}

} // namespace chronon3d
