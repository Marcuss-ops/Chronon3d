// ═══════════════════════════════════════════════════════════════════════════
// text_document_compile_result.hpp — INTERNAL accumulator types for the
// build_text_run pipeline (TICKET-092, text cat-3 #3)
//
// SCOPE — strictly INTERNAL:
//   This header lives in src/text/ (NOT in include/chronon3d/) and is
//   exposed ONLY to:
//     (a) src/text/text_run_builder.cpp (defines compile_text_document)
//     (b) tests/text/test_compile_text_layout_per_paragraph_failure.cpp
//         (regression lock for the per-paragraph failure contract)
//
//   Per AGENTS.md v0.1 cat-3 freeze, this is NOT installed with the
//   SDK and NOT part of the public API surface.  External consumers
//   must continue to call the public `build_text_run()` (text_run_builder.hpp)
//   which returns the legacy `TextRunBuildResult`.  The accumulator
//   pattern here exists to (a) preserve per-paragraph error info through
//   the pipeline instead of warn+skip, (b) let apply_spacing_collapse
//   use `source_index` as the bridge between the accumulator vector and
//   the resolved tree, (c) let tests inspect the per-paragraph outcome
//   without going through a stateful side channel.
//
// PIPELINE (build_text_run ↦ compile_text_document ↦ compile_text_layout):
//   For each paragraph i in the resolved tree, push a CompiledParagraphResult
//   entry holding:
//     - source_index = i (ordinal in the ORIGINAL doc / resolved tree)
//     - result      = Ok(layout) or Err(TextLayoutError)
//   `complete` is true iff every entry's `result` is Ok.
//
// REGRESSION LOCK (TICKET-092):
//   The previous `build_text_run` implementation did `spdlog::warn +
//   goto next_paragraph` for any per-paragraph error, silently dropping
//   the failure from the result vector.  This made it impossible for
//   callers to (a) detect that a specific paragraph failed, (b) know
//   which paragraph failed (no source_index), (c) preserve the
//   per-paragraph style chain across failures in apply_spacing_collapse.
//   The accumulator pattern here fixes all three by carrying the
//   `source_index` and the Err result alongside every Ok result.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/text/font_engine.hpp>          // FontEngine
#include <chronon3d/text/text_document.hpp>        // TextDocument
#include <chronon3d/text/text_run.hpp>             // TextRunLayout, TextLayoutCache
#include <chronon3d/text/text_run_builder.hpp>     // TextLayoutError, TextLayoutSpec

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace chronon3d {
namespace text_internal {

// ═══════════════════════════════════════════════════════════════════════════
// CompiledParagraphResult — one entry in the accumulator vector
// ═══════════════════════════════════════════════════════════════════════════

/// Per-paragraph compile outcome.  `source_index` is the ordinal of this
/// paragraph in the ORIGINAL `TextDocument::paragraphs` (and in the
/// resolved `ResolvedTextTree::paragraphs`); `result` carries either
/// the shaped `TextRunLayout` (Ok) or the structured `TextLayoutError`
/// (Err).  When a paragraph fails, the entry is still present in the
/// accumulator — callers inspect `result` to detect the failure and
/// use `source_index` to identify WHICH paragraph failed.
struct CompiledParagraphResult {
    /// Ordinal in the ORIGINAL document.  Lets apply_spacing_collapse
    /// bridge to the resolved tree's paragraphs[source_index].style
    /// regardless of how many earlier paragraphs also failed.
    std::size_t source_index{0};

    /// Ok(shared TextRunLayout) on success, Err(TextLayoutError) on
    /// per-paragraph failure (multi-font rejection, MissingFont,
    /// ShapingFailed, MalformedLayout).  Mirrors compile_text_layout's
    /// return type so no conversion is needed at the boundary.
    Result<std::shared_ptr<TextRunLayout>, TextLayoutError> result;
};

// ═══════════════════════════════════════════════════════════════════════════
// TextDocumentCompileResult — the full accumulator for one document
// ═══════════════════════════════════════════════════════════════════════════

/// The output of `compile_text_document()`.  Holds ONE entry per
/// paragraph in the ORIGINAL document, in document order.  `complete`
/// is true iff every entry is Ok; the wrapper `build_text_run` uses
/// this to detect partial failures and to log diagnostics while still
/// returning the legacy `TextRunBuildResult` (Ok-only) to external
/// callers.
struct TextDocumentCompileResult {
    /// Accumulator vector — length == ORIGINAL `doc.paragraphs.size()`.
    std::vector<CompiledParagraphResult> paragraphs;

    /// `true` iff EVERY entry is Ok.  `false` if at least one paragraph
    /// returned Err (multi-font rejection, MissingFont, ShapingFailed,
    /// or any other per-paragraph failure surfaced by compile_text_layout
    /// or the multi-font pre-check).
    bool complete{true};

    /// Total number of codepoints/clusters not covered by any font in the
    /// fallback stack across all paragraphs.
    std::size_t missing_glyph_count{0};
};

// ═══════════════════════════════════════════════════════════════════════════
// compile_text_document — INTERNAL accumulator
// ═══════════════════════════════════════════════════════════════════════════

/// Compile a `TextDocument` paragraph-by-paragraph, accumulating per-
/// paragraph outcomes (Ok AND Err) into a `TextDocumentCompileResult`.
///
/// Contract:
///   - `result.paragraphs.size() == doc.paragraphs.size()` (the ORIGINAL
///     doc's paragraph count, not the count of successful compiles).
///   - `result.paragraphs[i].source_index == i` (1:1 with doc order).
///   - `result.complete` is true iff every entry is Ok.
///   - apply_spacing_collapse is called internally AFTER accumulation;
///     Err paragraphs are SKIPPED by the spacing pass (no bounds to
///     adjust), and the spacing chain breaks at every Err.
///
/// This function is the canonical accumulator used by:
///   - `build_text_run()` (public wrapper, filters Ok + spdlog::warn)
///   - `apply_spacing_collapse()` (post-process, uses source_index)
///   - tests (regression lock for per-paragraph failure contract).
///
/// @param doc                 The TextDocument to compile.  Caller must have called
///                            `split_paragraphs()` first.
/// @param engine                FontEngine for shaping + fallback.
/// @param layout                Box / tracking / wrap / paragraph style spec.
/// @param cache                 Optional TextLayoutCache for cache lookup/store.
/// @param bundled_fonts_root    Directory scanned for bundled fallback fonts.
/// @return The accumulator.  Inspect `complete` for the document-level
///         outcome and `paragraphs[i]` for the per-paragraph outcome.
[[nodiscard]] TextDocumentCompileResult compile_text_document(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache = nullptr,
    const std::filesystem::path& bundled_fonts_root = {}
);

} // namespace text_internal
} // namespace chronon3d
