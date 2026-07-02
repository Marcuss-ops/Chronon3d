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

// P1-6 — Forward declaration of `ResolvedTextTree` (defined in
// `text_resolver.hpp`).  Used by the COMPILE_TEXT_LAYOUT-API additive
// `pre_resolved` parameter (P1-6 refactor); the implementation that
// resolves the tree lives in the implementation file.  Keeps this SDK
// public-header lean (no transitive include of the resolver).
struct ResolvedTextTree;

// ── Build result — one TextRunLayout per paragraph ─────────────────────

/// The output of build_text_run().  Contains one TextRunLayout per
/// paragraph in the source TextDocument, in document order.
///
/// `complete` is true when ALL paragraphs compiled successfully.
/// When false, one or more paragraphs were skipped (multi-font
/// rejection, MissingFont, ShapingFailed, etc.) and `paragraphs`
/// contains only the successful subset.  Callers MUST check
/// `complete` before assuming the result covers every paragraph.
struct TextRunBuildResult {
    /// Paragraph layouts (one per TextDocument paragraph).
    std::vector<std::shared_ptr<TextRunLayout>> paragraphs;

    /// True when every paragraph in the source document compiled
    /// successfully.  False when one or more paragraphs were skipped
    /// — `paragraphs.size()` may be less than the document's
    /// paragraph count.  Default `true` preserves backward
    /// compatibility for callers that don't inspect this field.
    bool complete{true};

    /// Total number of paragraphs in the result.
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

/// Compile-error taxonomy for the text pipeline
/// (compile_text_layout + compile_text_document).
/// Future extensions: `BidiFallback`.
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
    ///
    /// POLICY (verdict Issue #3, two-tier):
    ///   - `compile_text_layout()` (canonical single-paragraph compiler)
    ///     ACCEPTS multi-font paragraphs via the Phase 1.4 additive
    ///     `font_spans` path.  No rejection at this level — the renderer
    ///     switches BLFont at span boundaries.
    ///   - `compile_text_document()` (internal multi-paragraph accumulator)
    ///     and its public wrapper `build_text_run()` REJECT multi-font
    ///     paragraphs with `UnsupportedMultiFontRun`.  This protects the
    ///     public API surface: a caller that passes a full document
    ///     through `build_text_run()` must get homogeneous-font results.
    ///     Direct `compile_text_layout()` callers (e.g. the materializer)
    ///     can use the additive `font_spans` path without rejection.
    UnsupportedMultiFontRun,

    /// Per-run shaping failure: one or more ResolvedTextRun entries in
    /// a non-empty paragraph failed HarfBuzz shaping (returned zero
    /// glyphs despite having non-empty text).  Raised by
    /// compile_text_layout when ShapingFailurePolicy::FailWholeParagraph
    /// is active (the default).  Previously this was silently skipped —
    /// the merged PlacedGlyphRun still had glyphs from other runs, so
    /// the post-merge check `merged.glyphs.empty()` never fired.
    ///
    /// P1 #1 closure: per-run Result tracking replaces the implicit
    /// "skip and continue" with explicit policy.  See
    /// ShapingFailurePolicy below.
    PerRunShapingFailed,
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

/// Policy for handling per-run shaping failures in compile_text_layout.
///
/// When a ResolvedTextRun has non-empty text but HarfBuzz returns zero
/// glyphs, the compiler applies this policy instead of the previous
/// implicit "skip and continue" (which caused text to silently vanish
/// when one of several runs failed — P1 #1).
///
/// Default: FailWholeParagraph — any single-run failure causes the
/// entire paragraph to return Err(PerRunShapingFailed).
///
/// Future extensions (post-baseline):
///   - FallbackFont: retry with a fallback font
///   - ReplacementGlyph: insert a tofu/U+FFFD glyph
///   - PlaceholderDiagnostic: emit a visible placeholder for debugging
enum class ShapingFailurePolicy {
    /// Any run shaping failure → paragraph compile fails with
    /// Err(PerRunShapingFailed).  This is the safe default: no text
    /// is ever silently dropped.
    FailWholeParagraph,
};

/// Request type for `compile_text_layout`.  Borrows the caller's data
/// via non-owning pointers — the compile call is synchronous, lifetime
/// of `doc` / `layout` / `primary_font` is the caller's responsibility.
/// `primary_font` is used for cache-key construction (single-font
/// paragraphs only); empty `primary_font` falls back to
/// `doc->defaults.font`.
///
/// `paragraph_index` (TICKET-101, cat-3 freeze-compliant extension)
/// selects WHICH paragraph of `*doc` to compile.  `build_text_run()`
/// iterates 0..N and passes the i-th index; `materialize_text_run_shape`
/// passes 0.  Default 0 preserves backward compatibility for callers
/// that supply a single-paragraph TextDocument (the common case).
struct TextLayoutRequest {
    const TextDocument*   doc{nullptr};
    const TextLayoutSpec* layout{nullptr};
    FontSpec              primary_font{};
    std::size_t           paragraph_index{0};
    // TICKET-103a — extended POD fields so the cache-key signature
    // (TextLayoutCacheKey::digest) can honor bidi direction + BCP-47
    // language + OpenType shaping features without the legacy overrides
    // at src/text/text_run_builder.cpp:102-103 forcing
    // direction=Auto + language.clear().  Default-init keeps backward
    // compatibility for callers that aggregate-init with fewer fields
    // (compile_text_layout direct calls in tests pass {doc, layout, font}
    // and the new fields pick up their declared defaults).
    TextDirection         direction{TextDirection::Auto};
    Bcp47LanguageTag      language{};
    TextShapingFeatures   features{};
    /// P1 #1 — per-run shaping failure policy.  Default
    /// FailWholeParagraph: any single-run HarfBuzz failure causes the
    /// whole paragraph to return Err(PerRunShapingFailed).  This
    /// replaces the previous implicit "skip empty run and continue"
    /// which silently dropped text when one of several runs failed.
    ShapingFailurePolicy  shaping_failure_policy{ShapingFailurePolicy::FailWholeParagraph};
};

/// Single canonical TextRunLayout compiler.  Always populates `units`
/// on success — that is the bug-fix for shape.layout->units being
/// undefined after Scramble/Morph/CrossfadeLayouts transitions.
///
/// On failure returns `Err(TextLayoutError)` enumerating the cause.
/// On success returns a `SharedTextRunLayout` whose members
/// (`source_text`, `font`, `font_size`, `placed`, `units`, `bounds`,
/// `direction`, `line_height`) are all populated by construction.
///
/// P1-6 — additive `pre_resolved` parameter (optional, defaults to
/// nullptr): when the caller has ALREADY resolved the document into a
/// `ResolvedTextTree` (e.g. `compile_text_document` does so once at
/// doc level for spacing_collapse + multi-font pre-check), pass it in
/// to SKIP the per-paragraph re-resolution.  The N+1 -> 1 reduction is
/// particularly relevant for multi-paragraph subtitle presets (12+
/// paragraphs).  When `pre_resolved == nullptr` the function falls
/// back to its historical behaviour and resolves internally — direct
/// callers (materializer, tests) continue to compile unchanged.
[[nodiscard]] Result<
    std::shared_ptr<TextRunLayout>,
    TextLayoutError
> compile_text_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved = nullptr
);

} // namespace chronon3d
