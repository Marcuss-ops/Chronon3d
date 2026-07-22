// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_bidi_resolver.cpp
//
// M1.5#8 — single-responsibility module owning bidi segmentation for
// the text pipeline.  This TU wraps the engine-level bidi segmenter
// (segment_bidi_runs in src/backends/text/bidi_segmenter.cpp) and
// emits the per-run ResolvedTextRun entries after applying the font
// fallback chain.
//
// What lives here:
//   * `emit_via_bidi(sub, doc, engine, para, override_dir, out)` —
//     segments the sub-range through bidi, resolves the font via
//     FontResolver, and pushes ResolvedTextRun entries into `out`.
//   * The BidiRun → ResolvedTextRun adapter (the heart of the bidi
//     emission pipeline).
//
// What does NOT live here:
//   * Font fallback chain (text_font_resolver.cpp / FontResolver).
//   * Span splitting (text_span_resolver.cpp).
//   * Top-level orchestration (text_run_resolver.cpp).
//
// FriBidi optionality: `segment_bidi_runs` itself returns a single
// LTR run when CHRONON3D_HAS_FRIBIDI is undefined.  In addition,
// `CHRONON3D_FORCE_NO_FRIBIDI` (runtime env var) forces the LTR-only
// fallback path regardless of the compile flag — golden tests can
// exercise both branches from a single build.

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_resolver.hpp>     // ResolvedTextRun, ParagraphStyle
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/backends/text/bidi_segmenter.hpp>  // segment_bidi_runs + BidiRun

#include "src/text/resolver/font_resolver.hpp"
#include "src/text/resolver/font_fallback_resolver.hpp"
#include "src/text/resolver/text_span_resolver.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::text::resolver {

// ── Public surface declaration ────────────────────────────────────────────
//
// Kept here (rather than in a separate internal header) because the
// orchestrator (text_run_resolver.cpp) is the only consumer, and
// keeping the declaration co-located reduces include hops.
//
// `emit_via_bidi` — for one FontSubRange, segment through bidi,
// resolve the font through FontResolver, and push ResolvedTextRun
// entries into `out`.
//
// `bundled_fonts_root` — directory to scan for bundled fallback fonts
// (passed through to `make_default_font_stack`). Empty means "no bundled
// fonts available" (fail-loud via spdlog::warn, primary-only stack).

std::size_t emit_via_bidi(
    std::vector<ResolvedTextRun>&           out,
    const TextDocument&                     doc,
    FontEngine&                             engine,
    const FontSubRange&                     sub,
    const ParagraphRange&                   para,
    TextDirection                           override_dir,
    const std::filesystem::path&            bundled_fonts_root
);

} // namespace chronon3d::text::resolver

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::text::resolver {

namespace {

// Canonical fallback chain provided to FontResolver when the user did
// not supply FontRequest::extra_family_candidates.  Empty array + count 0
// = no extra-family step.  Kept here so the orchestrator's emit_via_bidi
// contract is one-line.
constexpr const char* kResolverExtras[]  = { nullptr };
constexpr std::size_t kResolverExtrasCnt = 0;

} // namespace

std::size_t emit_via_bidi(
    std::vector<ResolvedTextRun>&           out,
    const TextDocument&                     doc,
    FontEngine&                             engine,
    const FontSubRange&                     sub,
    const ParagraphRange&                   para,
    TextDirection                           override_dir,
    const std::filesystem::path&            bundled_fonts_root
) {
    std::string_view text = std::string_view(doc.utf8).substr(
        sub.byte_start, sub.byte_end - sub.byte_start);

    // Resolve the primary font through the canonical FontResolver first
    // (canonicalization + file-family fallback).  Then build the
    // cluster-level stack around it.
    FontRequest req;
    req.primary       = sub.font;
    req.extra_family_candidates      = kResolverExtras;
    req.extra_family_candidates_count = kResolverExtrasCnt;
    auto resolved = FontResolver{engine}.resolve(req);

    // Only expand the default fallback stack when the resolved primary
    // font is actually loadable.  This preserves fail-loud behaviour for
    // missing/corrupt primary fonts (the bundled fallbacks must not turn
    // an invalid font into a silent render).
    FontStack stack;
    if (engine.can_load(resolved.resolved)) {
        stack = make_default_font_stack(engine, resolved.resolved, bundled_fonts_root);
    } else {
        stack.push_back(resolved.resolved);
    }

    FontFallbackResolver fallback_resolver{engine};

    TextShaping shaping;
    shaping.direction = (override_dir != TextDirection::Auto) ? override_dir : TextDirection::Auto;
    if (!para.style.language.empty()) {
        shaping.language = para.style.language;
    }

    std::size_t missing_clusters = 0;

    // No bidi branch: even non-bidi-input collapses to a single run
    // with the resolved font + override_dir (or LTR fallback).
    auto runs = segment_bidi_runs(text);
    if (runs.empty()) {
        const TextDirection direction = (override_dir != TextDirection::Auto)
                                            ? override_dir : TextDirection::LTR;
        auto result = fallback_resolver.resolve_runs(
            text, stack, shaping, sub.byte_start, direction, para.style);
        missing_clusters += result.missing_clusters;
        for (auto& run : result.runs) {
            out.push_back(std::move(run));
        }
        return missing_clusters;
    }

    for (const auto& br : runs) {
        // When a paragraph has an explicit direction override, use it.
        // Otherwise use the bidi-resolved direction.
        const TextDirection direction = (override_dir != TextDirection::Auto)
                                            ? override_dir : br.direction;

        auto result = fallback_resolver.resolve_runs(
            br.text, stack, shaping,
            sub.byte_start + br.byte_offset,
            direction, para.style);

        missing_clusters += result.missing_clusters;
        for (auto& run : result.runs) {
            out.push_back(std::move(run));
        }
    }

    return missing_clusters;
}

} // namespace chronon3d::text::resolver
