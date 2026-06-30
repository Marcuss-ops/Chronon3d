#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_run_builder.hpp — Full-pipeline text run builder (PR 7)
//
// Unifies the entire text pipeline:
//   TextDocument → resolve_text_run_tree → HarfBuzz shape → place glyphs
//   → concatenate runs → ParagraphComposer → TextRunLayout
//
// Usage:
//   TextDocument doc = ...;
//   doc.split_paragraphs();
//   FontEngine engine;
//   TextLayoutSpec layout{.box = {800, 600}, .tracking = 1.0f, .paragraph = ...};
//
//   auto layouts = build_text_run(doc, engine, layout);
//   for (auto& l : layouts) {
//       // l.placed → shaped + positioned + composed glyphs
//       // l.bounds → final paragraph bounds
//   }
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_run.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

// ── Build result — one TextRunLayout per paragraph ─────────────────────

/// The output of build_text_run().  Contains one TextRunLayout per
/// paragraph in the source TextDocument, in document order.
struct TextRunBuildResult {
    /// Paragraph layouts (one per TextDocument paragraph).
    std::vector<std::shared_ptr<TextRunLayout>> paragraphs;

    /// Total number of paragraphs.
    [[nodiscard]] size_t size() const { return paragraphs.size(); }
};

// ═══════════════════════════════════════════════════════════════════════════
// build_text_run — full pipeline
// ═══════════════════════════════════════════════════════════════════════════

/// Build TextRunLayout entries from a TextDocument through the complete
/// text pipeline.
///
/// Pipeline per paragraph:
///   1. Resolve text runs (font fallback + bidi) via resolve_text_run_tree()
///   2. Shape each run via FontEngine::shape_text() with correct direction
///   3. Resolve glyph placements via resolve_placed_glyph_run()
///   4. Concatenate multiple PlacedGlyphRuns into one (handles font changes
///      within a paragraph)
///   5. Compose via ParagraphComposer (SingleLine or EveryLine depending
///      on ParagraphStyle::composer)
///   6. Build TextRunLayout with bounds, line_height, tracking, etc.
///
/// @param doc     The TextDocument to build layouts for.  MUST have
///                split_paragraphs() already called.
/// @param engine  FontEngine for shaping.
/// @param layout  Box dimensions, tracking, wrap mode, paragraph style, etc.
/// @param cache   Optional TextLayoutCache for reusing layouts across frames.
///                When non-null and cache_layout=true, build_text_run()
///                checks the cache before re-shaping.
/// @return A TextRunBuildResult with one TextRunLayout per paragraph.
[[nodiscard]] TextRunBuildResult build_text_run(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache = nullptr
);

// ═══════════════════════════════════════════════════════════════════════════
// compile_text_layout — single canonical TextRunLayout compiler (Fase 1.1)
// ═══════════════════════════════════════════════════════════════════════════
//
// Replaces the dual pipeline that previously lived across:
//   - `build_text_run()` above (populated the layout but NOT the
//     TextUnitMap — leaked an empty `units` field to per-frame code paths
//     and broke the selector's `total_units` query in Scramble/Morph/
//     CrossfadeLayouts drivers)
//   - `materialize_text_run_shape()` in `scene/builders/text_run_builder.cpp`
//     (populated the unit map, but via its OWN inline pipeline)
//
// After this refactor, both call sites route through `compile_text_layout`,
// guaranteeing the invariant: every successful return carries a valid
// `units` (TextUnitMap) — even zero-glyph paragraphs expose a valid empty
// Map with deterministic counts.  Per-frame drivers no longer observe
// `shape.layout->units` in an undefined state.
//
// `build_text_run()` is preserved as a backward-compatible wrapper that
// iterates paragraphs and routes each through `compile_text_layout()`
// (logging + skipping paragraphs that return `Err`).

/// Compile-error taxonomy for compile_text_layout.  Future extensions:
/// `BidiFallback`.
enum class TextLayoutErrorKind {
    /// Source text contains no UTF-8 bytes and no pre-split paragraphs.
    /// Catches the "render blank because caller forgot to set text"
    /// bug rather than silently emitting a zero-glyph layout whose
    /// unit map is empty (which freezes selectors at total_units=0).
    EmptySource,

    /// Shaping pipeline failed for a paragraph that DID have valid text
    /// AND a valid font spec — i.e. HarfBuzz rejected the input.  Distinct
    /// from MissingFont so callers can render a different fallback glyph
    /// (e.g. tofu replacement vs a system default).
    ShapingFailed,

    /// No usable font could be resolved for ANY run in the paragraph
    /// (all resolved FontSpec have empty path AND empty family).  Distinct
    /// from ShapingFailed so callers detect the "no system font available"
    /// state at compile time rather than producing a layout whose renderer
    /// will silently drop every glyph.
    MissingFont,

    /// Compile request itself is malformed (null pointers, out-of-range
    /// paragraph index, etc.).  Distinguished from EmptySource so a
    /// malformed call is loud at compile time rather than treated as
    /// "no text to render".
    MalformedLayout,

    /// Paragraph contains text-runs with DIFFERENT resolved FontSpec
    /// (different font_path / font_family / font_weight / font_style).
    /// Bug-fix candidate for verdict Issue #3: previously the layout
    /// kept `text_layout->font = doc.defaults.font` while glyph IDs came
    /// from N different fonts, producing the wrong glyph for some spans
    /// (tofu / wrong stroke / fill mismatch).  Stabilization strategy is
    /// to refuse the compile and surface a structured error rather than
    /// silently render corrupted glyphs.  A future atom adds
    /// `ShapedFontSpan` so the renderer can switch fonts at span
    /// boundaries and re-enable multi-font paragraphs.
    UnsupportedMultiFontRun,
};

/// Structured compile error returned in `Result`'s error channel.
/// `message` carries a one-line diagnostic for spdlog/debug; `kind`
/// gives callers programmatic switching (e.g. UI fallback policy).
struct TextLayoutError {
    TextLayoutErrorKind kind{TextLayoutErrorKind::EmptySource};
    std::string         message{};
};

/// Lightweight services bundle for `compile_text_layout`.  Intentionally
/// a STRICT SUBSET of the future full `TextServices` (FontStore,
/// GlyphAtlas, …) so this refactor stays atomic.  Migrating to the full
/// surface is a follow-up atom.
struct TextCompileServices {
    FontEngine*      engine{nullptr};
    TextLayoutCache* cache{nullptr};
};

/// Request type for `compile_text_layout`.  Borrows the caller's data
/// via non-owning pointers — the compile call is synchronous, lifetime
/// of `doc` / `layout` / `primary_font` is the caller's responsibility.
/// `primary_font` is used for cache-key construction (single-font
/// paragraphs only); empty `primary_font` falls back to
/// `doc->defaults.font`.
struct TextLayoutRequest {
    const TextDocument*   doc{nullptr};
    const TextLayoutSpec* layout{nullptr};
    FontSpec              primary_font{};
};

/// Single canonical TextRunLayout compiler.  Always populates `units`
/// on success — that is the bug-fix for shape.layout->units being
/// undefined after Scramble/Morph/CrossfadeLayouts transitions.
///
/// On failure returns `Err(TextLayoutError)` enumerating the cause.
/// On success returns a `SharedTextRunLayout` whose members
/// (`source_text`, `font`, `font_size`, `placed`, `units`, `bounds`,
/// `direction`, `line_height`) are all populated by construction.
[[nodiscard]] Result<
    std::shared_ptr<TextRunLayout>,
    TextLayoutError
> compile_text_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services
);

} // namespace chronon3d
