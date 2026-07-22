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

    // State machine: group consecutive codepoints that resolve to the same
    // font in the stack.
    std::size_t run_start          = 0;
    std::size_t current_font_index = 0; // valid while in a run
    bool        in_run             = false;
    std::size_t missing            = 0;
    std::vector<ResolvedTextRun> runs;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t cp_start = pos;
        const char32_t cp = unicode::decode_codepoint(text, pos);

        const std::size_t font_index = find_font_for_codepoint(cp, stack);

        if (font_index == stack.size()) {
            // Fail-loud audit: no font in the stack covers this codepoint.
            ++missing;
            spdlog::error(
                "[font-fallback] Missing glyph for codepoint U+{:04X} "
                "(no font in stack covers it)",
                static_cast<unsigned int>(cp));
        }

        if (!in_run) {
            run_start = cp_start;
            current_font_index = font_index;
            in_run = true;
        } else if (font_index != current_font_index) {
            // Flush the previous run.
            ResolvedTextRun run;
            run.text = std::string(text.substr(run_start, cp_start - run_start));
            run.font = (current_font_index < stack.size())
                           ? stack.fonts[current_font_index]
                           : stack.fonts[0]; // missing → primary as tofu sink
            run.direction = direction;
            run.byte_offset = base_byte_offset + run_start;
            run.byte_len = cp_start - run_start;
            run.paragraph_style = style;
            runs.push_back(std::move(run));

            // Start a new run from this codepoint.
            run_start = cp_start;
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

// ── find_font_for_codepoint ────────────────────────────────────────────────
std::size_t FontFallbackResolver::find_font_for_codepoint(
    char32_t cp,
    const FontStack& stack
) const {
    for (std::size_t i = 0; i < stack.fonts.size(); ++i) {
        // Cat-5 internal: the glyph-coverage probe lives in the
        // `font_engine_internal` namespace, friend-declared on FontEngine;
        // not in the public FontEngine API.
        if (chronon3d::text::font_engine_internal::has_glyph_for_codepoint(
                engine_, stack.fonts[i], cp)) {
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
