// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/font_fallback_resolver.cpp
//
// Canonical cluster-level font fallback implementation.  See the header
// for the architectural contract.

#include "src/text/resolver/font_fallback_resolver.hpp"

// Cat-5 internal: glyph coverage probe is exposed via the
// `font_engine_internal` namespace (free function, friend-declared on
// FontEngine). This avoids expanding the public FontEngine ABI.
#include "src/backends/text/font_engine_internal.hpp"

#include "src/text/unicode/utf8_decoder.hpp"

#include <chronon3d/backends/text/text_unicode_utils.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::text::resolver {

namespace {

// Scan `bundled_fonts_root` (e.g. `<assets_root>/fonts`) for loadable
// `.ttf` files. The path is supplied by the caller — typically the
// `RenderRuntime::config().assets_root` (or similar) — so behaviour is
// independent of the current working directory.
//
// Fail-loud: missing directory or enumeration failure is logged via
// spdlog::warn / spdlog::error, never silently swallowed.
[[nodiscard]] std::vector<FontSpec> scan_bundled_fallback_fonts(
    FontEngine& engine,
    const std::filesystem::path& bundled_fonts_root
) {
    std::vector<FontSpec> specs;
    if (bundled_fonts_root.empty()) {
        spdlog::warn(
            "[font-fallback] No bundled_fonts_root provided — default "
            "fallback stack degrades to primary-only.");
        return specs;
    }
    if (!std::filesystem::exists(bundled_fonts_root)) {
        spdlog::warn(
            "[font-fallback] Bundled fonts directory not found: '{}'. "
            "Default fallback stack degrades to primary-only.",
            bundled_fonts_root.string());
        return specs;
    }
    std::error_code ec;
    auto iter = std::filesystem::directory_iterator(bundled_fonts_root, ec);
    if (ec) {
        spdlog::error(
            "[font-fallback] Failed to enumerate bundled fonts "
            "directory '{}': {}",
            bundled_fonts_root.string(), ec.message());
        return specs;
    }
    for (const auto& entry : iter) {
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec) || entry_ec) continue;
        if (entry.path().extension() != ".ttf") continue;
        FontSpec spec;
        spec.font_path = entry.path().generic_string();
        // Populate family/style/weight from the face so downstream
        // consumers (tests, identity, diagnostics) see meaningful
        // metadata instead of an empty family string. A true result
        // also means the font is loadable, so we skip the separate
        // can_load() call here.
        std::string family, style;
        int weight = 0;
        if (chronon3d::text::font_engine_internal::inspect_font(engine, spec, family, style, weight)) {
            spec.font_family = std::move(family);
            spec.font_style  = std::move(style);
            spec.font_weight = weight;
            specs.push_back(std::move(spec));
        }
    }
    return specs;
}

} // namespace

// ── FontFallbackResolver::resolve_runs ─────────────────────────────────────
FontFallbackResult FontFallbackResolver::resolve_runs(
    std::string_view     text,
    const FontStack&     stack,
    const TextShaping&   /*shaping*/,
    std::size_t          base_byte_offset,
    TextDirection        direction,
    const ParagraphStyle& style
) {
    FontFallbackResult result;
    result.missing_clusters = 0;
    if (text.empty() || stack.empty()) {
        return result;
    }

    // State machine: group consecutive *grapheme clusters* that resolve to
    // the same font in the stack.  Extended grapheme clusters keep combining
    // marks, ZWJ emoji sequences, regional-indicator flag pairs, etc. together
    // so they are never split across font runs (Cat-3 anti-duplication:
    // exactly one canonical cluster-fallback path).
    std::size_t run_start          = 0;
    std::size_t current_font_index = 0; // valid while in a run
    bool        in_run             = false;
    std::size_t missing            = 0;
    std::vector<ResolvedTextRun> runs;

    const std::size_t cluster_count = chronon3d::detail::grapheme_cluster_count(text);
    for (std::size_t i = 0; i < cluster_count; ++i) {
        const std::size_t cluster_start =
            (i == 0) ? 0 : chronon3d::detail::grapheme_byte_offset_at(text, i);
        const std::size_t cluster_end =
            chronon3d::detail::grapheme_byte_offset_at(text, i + 1);
        const std::string_view cluster =
            text.substr(cluster_start, cluster_end - cluster_start);

        const std::size_t font_index =
            find_font_for_cluster(cluster, stack);

        if (font_index == stack.size()) {
            // Fail-loud audit: no font in the stack covers this cluster.
            ++missing;

            std::string codepoints;
            constexpr std::size_t kMaxLoggedCodepoints = 8;
            std::size_t           logged               = 0;
            std::size_t           probe                = 0;
            while (probe < cluster.size() && logged < kMaxLoggedCodepoints) {
                const char32_t cp = unicode::decode_codepoint(cluster, probe);
                if (!codepoints.empty()) codepoints += ' ';
                codepoints += fmt::format("U+{:04X}", static_cast<unsigned int>(cp));
                ++logged;
            }
            if (probe < cluster.size()) {
                codepoints += " ...";
            }

            spdlog::error(
                "[font-fallback] Missing glyph for cluster [{}] at byte offset "
                "{}..{} (no font in stack covers all visible codepoints)",
                codepoints,
                base_byte_offset + cluster_start,
                base_byte_offset + cluster_end);
        }

        if (!in_run) {
            run_start = cluster_start;
            current_font_index = font_index;
            in_run = true;
        } else if (font_index != current_font_index) {
            // Flush the previous run.
            ResolvedTextRun run;
            run.text = std::string(text.substr(run_start, cluster_start - run_start));
            run.font = (current_font_index < stack.size())
                           ? stack.fonts[current_font_index]
                           : stack.fonts[0]; // missing → primary as tofu sink
            run.direction = direction;
            run.byte_offset = base_byte_offset + run_start;
            run.byte_len = cluster_start - run_start;
            run.paragraph_style = style;
            runs.push_back(std::move(run));

            // Start a new run from this cluster.
            run_start = cluster_start;
            current_font_index = font_index;
        }
    }

    // Flush the final run.
    if (in_run) {
        ResolvedTextRun run;
        run.text = std::string(text.substr(run_start, text.size() - run_start));
        run.font = (current_font_index < stack.size())
                       ? stack.fonts[current_font_index]
                       : stack.fonts[0];
        run.direction = direction;
        run.byte_offset = base_byte_offset + run_start;
        run.byte_len = text.size() - run_start;
        run.paragraph_style = style;
        runs.push_back(std::move(run));
    }

    result.missing_clusters = missing;
    result.runs = std::move(runs);
    return result;
}

// ── find_font_for_cluster ──────────────────────────────────────────────────
std::size_t FontFallbackResolver::find_font_for_cluster(
    std::string_view cluster,
    const FontStack& stack
) const {
    // Decode the cluster once so we do not re-parse UTF-8 for every font in
    // the stack.  A font covers a cluster when it covers every visible
    // codepoint in it.  Invisible codepoints (ZWJ, variation selectors,
    // whitespace, controls, etc.) are treated as covered by any font by the
    // underlying probe, so they do not force a fallback.
    std::vector<char32_t> cps;
    cps.reserve(8);
    std::size_t pos = 0;
    while (pos < cluster.size()) {
        cps.push_back(unicode::decode_codepoint(cluster, pos));
    }

    for (std::size_t i = 0; i < stack.fonts.size(); ++i) {
        bool covers = true;
        for (char32_t cp : cps) {
            if (!chronon3d::text::font_engine_internal::has_glyph_for_codepoint(
                    engine_, stack.fonts[i], cp)) {
                covers = false;
                break;
            }
        }
        if (covers) {
            return i;
        }
    }
    return stack.size(); // sentinel: uncovered
}

// ── make_default_font_stack ────────────────────────────────────────────────
FontStack make_default_font_stack(
    FontEngine& engine,
    const FontSpec& primary,
    const std::filesystem::path& bundled_fonts_root
) {
    FontStack stack;
    stack.push_back(primary);

    // Scan the supplied root every time. The directory is typically small,
    // and caching by process lifetime would ignore different roots passed by
    // different callers (e.g., tests vs. runtime).
    const std::vector<FontSpec> bundled =
        scan_bundled_fallback_fonts(engine, bundled_fonts_root);
    for (const auto& cand : bundled) {
        if (cand.font_path == primary.font_path) continue;
        // Fonts are already validated by inspect_font() during scanning,
        // so they are loadable. Skip the redundant can_load() call.
        stack.push_back(cand);
    }

    return stack;
}

} // namespace chronon3d::text::resolver
