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

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_run.hpp>

#include <memory>
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

} // namespace chronon3d
