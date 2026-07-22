// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/font_fallback_resolver.hpp — INTERNAL
//
// Canonical cluster-level font fallback service.  Given a UTF-8 text and an
// ordered font stack, it verifies glyph coverage for each codepoint using
// FontEngine::has_glyph_for_codepoint (FreeType / HarfBuzz nominal glyph),
// groups consecutive codepoints that resolve to the same font, and emits
// one ResolvedTextRun per group.  Shaping is performed later by the existing
// shape_resolved_run path, so there is no second shaper in the renderer.
//
// The service is strictly internal to src/text/resolver/ and is the single
// canonical path for grapheme/cluster fallback (Cat-3 anti-duplication).

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/text/text_direction.hpp>

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace chronon3d::text::resolver {

// ── FontStack — ordered list of fonts to try for each codepoint ────────────
struct FontStack {
    std::vector<FontSpec> fonts;

    [[nodiscard]] bool empty() const noexcept { return fonts.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return fonts.size(); }

    void push_back(FontSpec spec) { fonts.push_back(std::move(spec)); }
};

// ── FontFallbackResult — output of the fallback resolver ───────────────────
struct FontFallbackResult {
    /// Number of codepoints/clusters not covered by any font in the stack.
    /// A non-zero value is always logged via spdlog::error (fail-loud).
    std::size_t missing_clusters{0};

    /// Resolved text runs, each with a single font that covers its text.
    std::vector<ResolvedTextRun> runs;

    FontFallbackResult() = default;
    FontFallbackResult(std::size_t missing, std::vector<ResolvedTextRun> in_runs)
        : missing_clusters(missing), runs(std::move(in_runs)) {}
};

/// Alias for the canonical output of the fallback resolver.  Used by
/// callers that prefer a service-oriented name (`ResolvedFontRunList`).
using ResolvedFontRunList = FontFallbackResult;

// ── FontFallbackResolver — single canonical cluster fallback service ───────
//
// The resolver holds a reference to the live FontEngine.  It performs only
// nominal-glyph coverage probes (no shaping) and is therefore cheap compared
// to a full HarfBuzz pass.
class FontFallbackResolver {
public:
    explicit FontFallbackResolver(FontEngine& engine) noexcept
        : engine_(engine) {}

    // Non-copyable; stack instances are cheap.
    FontFallbackResolver(const FontFallbackResolver&)            = delete;
    FontFallbackResolver& operator=(const FontFallbackResolver&) = delete;
    FontFallbackResolver(FontFallbackResolver&&) noexcept        = default;
    FontFallbackResolver& operator=(FontFallbackResolver&&) noexcept = default;

    /// Resolve a text into one or more runs, each using a single font from
    /// `stack` that covers all codepoints in the run.
    ///
    /// @param text             UTF-8 text to resolve.
    /// @param stack            Ordered list of fonts to try.
    /// @param shaping          Shaping parameters (direction is taken from
    ///                         `direction` unless overridden).
    /// @param base_byte_offset   Byte offset of `text` in the original document,
    ///                         copied into each run's byte_offset.
    /// @param direction        Text direction for all emitted runs.
    /// @param style            Paragraph style propagated to each run.
    /// @return A FontFallbackResult with the resolved runs and a count of
    ///         uncovered codepoints (logged as errors).
    [[nodiscard]] FontFallbackResult resolve_runs(
        std::string_view     text,
        const FontStack&     stack,
        const TextShaping&   shaping,
        std::size_t          base_byte_offset,
        TextDirection        direction,
        const ParagraphStyle& style
    );

private:
    FontEngine& engine_;

    // Returns the index of the first font in `stack` that covers `cp`,
    // or `stack.size()` if none does.
    [[nodiscard]] std::size_t find_font_for_codepoint(char32_t cp, const FontStack& stack) const;
};

/// Build a default fallback stack for a resolved primary font.  The stack
/// includes the primary font followed by bundled fallback fonts that can
/// actually be loaded by the engine. The caller supplies the bundled-
/// fonts root path (typically `<runtime.assets_root>/fonts`) so behaviour
/// is independent of the current working directory.
[[nodiscard]] FontStack make_default_font_stack(
    FontEngine& engine,
    const FontSpec& primary,
    const std::filesystem::path& bundled_fonts_root
);

} // namespace chronon3d::text::resolver
