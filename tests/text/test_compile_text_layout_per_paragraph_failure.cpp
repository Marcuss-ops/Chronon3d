// ═══════════════════════════════════════════════════════════════════════════
// test_compile_text_layout_per_paragraph_failure.cpp
//
// TICKET-092 (text cat-3 #3) regression suite for `compile_text_document`
// (the INTERNAL accumulator introduced in src/text/) and the source_index
// bridge in `apply_spacing_collapse`.
//
// What this TU locks:
//   - TEST 1: 3-paragraph doc with a multi-font CENTRAL paragraph
//     (paragraph 1 of 3).  The accumulator returns:
//       * result.paragraphs.size() == 3  (NOT 2 — no skip-on-failure)
//       * result.paragraphs[0].source_index == 0, result is Ok
//       * result.paragraphs[1].source_index == 1, result is Err(UnsupportedMultiFontRun)
//       * result.paragraphs[2].source_index == 2, result is Ok
//       * result.complete == false
//     The Err entry carries the source_index that lets callers identify
//     WHICH paragraph failed, instead of inferring it from a count drop.
//
//   - TEST 2: single-paragraph doc with a multi-font paragraph.  The
//     accumulator returns a single Err entry with source_index == 0
//     and complete == false.  Regression lock that the accumulator does
//     NOT swallow the failure (the previous warn+skip pattern would
//     have returned an empty result.paragraphs vector with size 0).
//
//   - TEST 3: 3-paragraph doc with all single-font paragraphs.  The
//     accumulator returns 3 Ok entries, complete == true.  Sanity check
//     that the accumulator behaves identically to a "no failure" path
//     and is purely additive (no semantic change for the success case).
//
// IMPORTANT: this TU MUST stay synchronous.  Each test exercises a
// STATIC FEATURE of the input geometry (divergent fonts, single-font)
// rather than the resolver's behaviour with system fonts.  The
// LocalEngine fixture is per-TEST_CASE; system fonts may or may not
// be installed — assertions on Ok/Err outcomes do not depend on
// shaping (the multi-font check fires on the RESOLVED tree before any
// shaping happens; the compile success / Ok assertions only inspect
// shape.layout != nullptr / shape.units is a valid map).
// ═══════════════════════════════════════════════════════════════════════════

// TICKET-092: relative include of the INTERNAL accumulator header.
// NOT under include/chronon3d/ (cat-3 freeze).  Lives in src/text/.
// Tests must reach the header by relative path so the install surface
// is unchanged.
#include "../../src/text/text_document_compile_result.hpp"

#include <chronon3d/text/text_run_builder.hpp>     // TextLayoutErrorKind, TextLayoutError
#include <chronon3d/text/text_resolver.hpp>        // (transitively via header)

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <string>

using namespace chronon3d;
using text_internal::CompiledParagraphResult;
using text_internal::TextDocumentCompileResult;
using text_internal::compile_text_document;

namespace {

/// RAII-owned engine stack: do NOT use shared singletons across tests.
/// Same fixture pattern as test_compile_text_layout_errors.cpp +
/// test_rich_text_paragraph_preservation.cpp.
struct LocalEngine {
    chronon3d::Config                cfg{};
    chronon3d::runtime::RenderRuntime runtime;
    FontEngine                        engine;

    LocalEngine()
        : runtime(cfg),
          engine{runtime.resolver()}
    {}
};

/// Build a 3-paragraph doc: "Line A" / middle / "Line C".  The middle
/// paragraph may carry a divergent-font span (TICKET-101 multi-font
/// pattern) depending on `middle_multi_font` — when true, the middle
/// paragraph's runs will diverge in resolved FontSpec (the BACK half
/// of "AAAABBBB" uses a different family).
TextDocument make_three_para_doc_with_middle(
    bool middle_multi_font
) {
    constexpr const char* kLeading   = "Line A\n";
    constexpr const char* kMiddle    = "AAAABBBB";
    constexpr const char* kTrailing  = "\nLine C";
    const std::size_t leading_len = std::strlen(kLeading);
    const std::size_t middle_start = leading_len;
    const std::size_t middle_end   = leading_len + std::strlen(kMiddle);

    TextDocument doc;
    doc.utf8 = std::string(kLeading) + kMiddle + kTrailing;
    doc.defaults.font.font_family = "DejaVu Sans";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    if (middle_multi_font) {
        // Span on the back half of the middle paragraph with a
        // different family — after resolve, the middle paragraph has
        // 2 runs with divergent FontSpec.
        TextStyleSpan override_span;
        override_span.byte_start = middle_start + 4;  // first 'B' in middle
        override_span.byte_end   = middle_end;        // after last 'B'
        override_span.font = FontSpec{};
        override_span.font->font_family = "Liberation Serif";
        override_span.font->font_size   = 32.0f;
        override_span.font->font_weight = 400;
        doc.spans.push_back(override_span);
    }

    return doc;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Central paragraph fails — accumulator preserves all 3 entries,
//    complete=false, source_index identifies the failing paragraph.
// ═══════════════════════════════════════════════════════════════════════════
//
// TICKET-092 contract: a 3-paragraph doc where the MIDDLE paragraph
// fails (multi-font) MUST yield an accumulator with 3 entries
// (Ok, Err, Ok), not 2 (the previous warn+skip pattern).  The
// complete flag is false; the failing entry's source_index == 1.
//
// This is the regression lock for the per-paragraph failure contract.
// The previous `spdlog::warn + goto next_paragraph` pattern would have
// produced a 2-entry TextRunBuildResult with no way to tell from the
// return value that paragraph 1 (the central one) was the one that
// was rejected — only the count drop was visible.
TEST_CASE("compile_text_document: central paragraph fails — all 3 CompiledParagraphResult preserved, complete=false") {
    LocalEngine env;

    // 3 paragraphs, middle one multi-font (deterministic regardless of
    // system font availability — the multi-font check fires on the
    // RESOLVED tree before any shaping).
    auto doc = make_three_para_doc_with_middle(/*middle_multi_font=*/true);
    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 600.0f};

    // ── Invoke the INTERNAL accumulator directly. ──
    auto compile_result = compile_text_document(doc, env.engine, layout, /*cache=*/nullptr);

    // ── INVARIANT (TICKET-092 #1): all 3 entries present. ──
    REQUIRE(compile_result.paragraphs.size() == 3);

    // ── INVARIANT (TICKET-092 #2): source_index is the ORIGINAL ordinal. ──
    CHECK(compile_result.paragraphs[0].source_index == 0);
    CHECK(compile_result.paragraphs[1].source_index == 1);
    CHECK(compile_result.paragraphs[2].source_index == 2);

    // ── INVARIANT (TICKET-092 #3): per-paragraph outcome is preserved. ──
    // First paragraph (source=0, single-font "Line A") compiles Ok.
    CHECK(compile_result.paragraphs[0].result.is_ok());
    if (compile_result.paragraphs[0].result.is_ok()) {
        const auto& layout0 = *compile_result.paragraphs[0].result.value();
        CHECK(layout0.units.units.empty() == false || layout0.source_text.empty() == false);
    }

    // Central paragraph (source=1, multi-font "AAAABBBB") is REJECTED
    // by the multi-font pre-check — Err with UnsupportedMultiFontRun.
    // This is the entry the previous warn+skip pattern would have
    // silently dropped; here it is preserved with the source_index
    // bridge intact.
    CHECK(compile_result.paragraphs[1].result.is_err());
    REQUIRE(compile_result.paragraphs[1].result.is_err());
    CHECK(compile_result.paragraphs[1].result.error().kind
          == TextLayoutErrorKind::UnsupportedMultiFontRun);
    CHECK_FALSE(compile_result.paragraphs[1].result.error().message.empty());

    // Last paragraph (source=2, single-font "Line C") compiles Ok.
    CHECK(compile_result.paragraphs[2].result.is_ok());
    if (compile_result.paragraphs[2].result.is_ok()) {
        const auto& layout2 = *compile_result.paragraphs[2].result.value();
        CHECK(layout2.units.units.empty() == false || layout2.source_text.empty() == false);
    }

    // ── INVARIANT (TICKET-092 #4): complete flag reflects ANY Err. ──
    CHECK(compile_result.complete == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Single-paragraph doc with multi-font — accumulator returns 1 Err
//    entry with source_index == 0 and complete == false.
// ═══════════════════════════════════════════════════════════════════════════
//
// Regression lock: a single-paragraph doc that fails to compile MUST
// return a non-empty accumulator with the Err preserved.  The previous
// warn+skip pattern would have returned an empty TextRunBuildResult
// (size == 0) — silently indistinguishable from "empty input".
//
// The accumulator fixes this: size == 1, source_index == 0, Err
// carried, complete == false.
TEST_CASE("compile_text_document: single-paragraph doc with multi-font — 1 Err entry, complete=false") {
    LocalEngine env;

    TextDocument doc;
    doc.utf8 = "HelloWorld";
    doc.defaults.font.font_family = "DejaVu Sans";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    // Multi-font span: override family on the back half.
    TextStyleSpan override_span;
    override_span.byte_start = 5;     // "Worl" overrides family
    override_span.byte_end   = 10;    // "d" inherits
    override_span.font = FontSpec{};
    override_span.font->font_family = "Liberation Serif";
    override_span.font->font_size   = 32.0f;
    override_span.font->font_weight = 400;
    doc.spans.push_back(override_span);

    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto compile_result = compile_text_document(doc, env.engine, layout, /*cache=*/nullptr);

    // Single-paragraph accumulator: exactly 1 entry, source_index == 0.
    REQUIRE(compile_result.paragraphs.size() == 1);
    CHECK(compile_result.paragraphs[0].source_index == 0);

    // Multi-font pre-check rejects the paragraph.
    CHECK(compile_result.paragraphs[0].result.is_err());
    REQUIRE(compile_result.paragraphs[0].result.is_err());
    CHECK(compile_result.paragraphs[0].result.error().kind
          == TextLayoutErrorKind::UnsupportedMultiFontRun);

    // complete is false (one or more Err entries).
    CHECK(compile_result.complete == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. All-single-font 3-paragraph doc — accumulator returns 3 Ok entries,
//    complete=true.  Sanity check: success case is purely additive.
// ═══════════════════════════════════════════════════════════════════════════
//
// Regression lock: the accumulator pattern is PURELY ADDITIVE for the
// success case.  A 3-paragraph doc with no multi-font paragraph
// (paragraph 1 is single-font "AAAABBBB") returns 3 Ok entries and
// complete == true.  The per-paragraph invariants (Fase 1.1
// `units` populated, source_text preserved) hold for each entry.
TEST_CASE("compile_text_document: all-single-font 3-paragraph doc — 3 Ok entries, complete=true") {
    LocalEngine env;

    // 3 paragraphs, all single-font.  Middle paragraph is NOT multi-font
    // (the override_span would have flipped family — we omit it here).
    auto doc = make_three_para_doc_with_middle(/*middle_multi_font=*/false);
    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 600.0f};

    auto compile_result = compile_text_document(doc, env.engine, layout, /*cache=*/nullptr);

    // 3 entries, all Ok, source_index 1:1 with doc paragraphs.
    REQUIRE(compile_result.paragraphs.size() == 3);
    CHECK(compile_result.paragraphs[0].source_index == 0);
    CHECK(compile_result.paragraphs[1].source_index == 1);
    CHECK(compile_result.paragraphs[2].source_index == 2);

    // All three compile Ok (the multi-font pre-check does NOT fire
    // because each paragraph resolves to a single run with the same
    // family; system fonts may or may not be installed — the
    // structural invariant is the Ok/Err classification itself).
    CHECK(compile_result.paragraphs[0].result.is_ok());
    CHECK(compile_result.paragraphs[1].result.is_ok());
    CHECK(compile_result.paragraphs[2].result.is_ok());

    // complete is true (zero Err entries).
    CHECK(compile_result.complete == true);

    // Per-paragraph invariants (Fase 1.1): Ok ⇒ units populated.
    for (const auto& entry : compile_result.paragraphs) {
        REQUIRE(entry.result.is_ok());
        const auto& l = *entry.result.value();
        CHECK_FALSE(l.source_text.empty());
    }
}
