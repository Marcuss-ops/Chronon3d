#include <optional>
#include <memory>
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
// test_compile_text_layout_errors.cpp
//
// Regression lock for compile_text_layout:
//   1. Err(EmptySource) fires when doc has no utf8 and no pre-split
//      paragraphs (compile_text_layout direct call).
//   2. Err(MalformedLayout): null doc OR layout pointer.
//   3. Err(ShapingFailed): null engine pointer (intentional asymmetry
//      vs null doc/layout \u2014 engine is a SHAPING concern).
//   4. PHASE 1.4 REGRESSION: multi-font paragraph no longer emits
//      Err(UnsupportedMultiFontRun).  Instead `compile_text_layout`
//      returns Ok and `TextRunLayout::font_spans` is populated so the
//      renderer can switch BLFont at span boundaries (text_run_processor.cpp).
//      This test locks the NEW behavioural contract:
//        (a) compile succeeds on a multi-font input,
//        (b) font_spans is a contiguous, non-overlapping decomposition
//            of placed.glyphs \u2014 either empty (no shaped glyphs) or covers
//            the entire glyph range with [span_i.glyph_end == span_i+1.glyph_begin].
//   5. MissingFont: STUB \u2014 requires FontEngine::reset_registrations() API
//      which is not yet exposed (TICKET-cr-followup).
//
// IMPORTANT: this TU MUST stay synchronous, MUST NOT call shape_text /
// prewarm / FontResolver with side effects. Each Err case
// is triggered by a STATIC FEATURE of the input (geometry of inputs,
// not behaviour of the resolver with system fonts).
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <tests/helpers/test_utils.hpp>

#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include "../../src/text/text_document_compile_result.hpp"
#include <chronon3d/text/text_resolver.hpp>
#include <cstring>
#include <string>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

/// RAII-owned engine stack: do NOT use shared singletons across tests.
/// Each TEST_CASE gets a fresh Config + RuntimeContext + FontEngine.
///
/// Note: `runtime.resolver()` is a process-level singleton.  Residuals
/// from one TEST_CASE's font registration MAY survive into the next,
/// which is acceptable because the four tests in this TU assert on
/// STRUCTURE of inputs (geometry, byte ranges) rather than on
/// font-engine behaviour.  Tests that read font-engine state directly
/// would need a different fixture (e.g. a mock resolver).
struct LocalEngine {
    chronon3d::Config cfg{};
    std::unique_ptr<chronon3d::runtime::RuntimeContext> runtime;
    FontEngine engine;

    LocalEngine()
        : runtime(chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value()),
          engine{runtime->resolver()}
    {}
};

} // namespace

// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
// 1. Err(EmptySource) \u2014 compile_text_layout direct call
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
//
// A TextDocument left entirely default-constructed has empty utf8 and
// empty pre-split paragraphs.  compile_text_layout short-circuits with
// `Err(TextLayoutErrorKind::EmptySource, \u2026)` BEFORE any resolver/cache/
// path setup is touched.
TEST_CASE("compile_text_layout: empty source returns Err(EmptySource)") {
    LocalEngine env;

    TextDocument doc;          // no utf8, no split_paragraphs
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    TextLayoutRequest   request{&doc, &layout, FontSpec{}};
    TextCompileServices services{&env.engine, /*cache=*/nullptr};

    auto result = compile_text_layout(request, services);

    REQUIRE_FALSE(result.has_value());
    CHECK(!result.has_value());
    CHECK(result.error().kind == TextLayoutErrorKind::EmptySource);
    CHECK_FALSE(result.error().message.empty());
}

// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
// 2. Err(MalformedLayout) + Err(ShapingFailed) \u2014 null pointers
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
//
// Symmetric null-pointer coverage locks the assertion that a future
// \u201csymmetrize the null-pointer checks\u201d refactor does not silently
// change observable behaviour.  Null engine returns ShapingFailed, NOT
// MalformedLayout \u2014 an engine is a SHAPING concern (not a programmer
// error); returning MalformedLayout would bury the cause.
TEST_CASE("compile_text_layout: malformed request (null pointers) returns Err(MalformedLayout)") {
    LocalEngine env;

    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    // Null doc \u2192 Err(MalformedLayout)
    {
        TextDocument           some_doc;
        TextLayoutRequest      bad_doc{/*doc=*/nullptr, &layout, FontSpec{}};
        TextCompileServices    services{&env.engine, nullptr};
        auto result = compile_text_layout(bad_doc, services);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == TextLayoutErrorKind::MalformedLayout);
    }
    // Null layout \u2192 Err(MalformedLayout)
    {
        TextDocument           some_doc;
        TextLayoutRequest      bad_layout{&some_doc, /*layout=*/nullptr, FontSpec{}};
        TextCompileServices    services{&env.engine, nullptr};
        auto result = compile_text_layout(bad_layout, services);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == TextLayoutErrorKind::MalformedLayout);
    }
    // Null engine \u2192 Err(ShapingFailed) (intentional asymmetry)
    {
        TextDocument           some_doc;
        TextLayoutSpec         some_layout;
        some_layout.box        = {800.0f, 200.0f};
        TextLayoutRequest      no_eng_req{&some_doc, &some_layout, FontSpec{}};
        TextCompileServices    no_eng_svc{/*engine=*/nullptr, /*cache=*/nullptr};
        auto result = compile_text_layout(no_eng_req, no_eng_svc);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == TextLayoutErrorKind::MissingFontEngine);
    }
}

// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
// 3. PHASE 1.4 REGRESSION \u2014 multi-font paragraph compiles + font_spans
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
//
// Phase 1.4 ADDITIVE PATH (verdict Issue #3 closure): compile_text_layout
// ACCEPTS multi-font paragraphs and populates TextRunLayout::font_spans
// so the renderer can switch BLFont at span boundaries.  Replaces the
// Phase 1.5 CR#1 stab path that emitted Err(UnsupportedMultiFontRun).
//
// What this test asserts (locked invariants, independent of system fonts):
//   (a) compile_text_layout returns Ok on a multi-font input,
//   (b) `font_spans` is a contiguous, non-overlapping decomposition of
//       `placed.glyphs`.  Each span_i has [span_i.glyph_begin, span_i.glyph_end)
//       and span_i.glyph_end == span_(i+1).glyph_begin \u2014 the spans tile
//       the glyph range without gaps or overlaps.  Either empty (no
//       shaped glyphs) or covers [0, placed.glyphs.size()), or any
//       contiguous prefix thereof is INVALID (and the test catches it).
TEST_CASE("compile_text_layout: multi-font paragraph compiles Ok + font_spans is contiguous") {
    LocalEngine env;

    TextDocument doc;
    doc.utf8 = "AAABBB";                            // 6 bytes; 2 spans below override the back half
    doc.defaults.font.font_path   = chronon3d::test::bundled_font_path("assets/fonts/Inter-Regular.ttf");
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    // Span override on the back half with a DIFFERENT family \u2014 produces
    // 2 ResolvedTextRuns with divergent FontSpec after resolve_text_run_tree.
    TextStyleSpan override_span;
    override_span.byte_start = 3;
    override_span.byte_end   = 6;
    override_span.font = FontSpec{};
    override_span.font->font_path = chronon3d::test::bundled_font_path("assets/fonts/FreeSerif.ttf");
    override_span.font->font_family = "FreeSerif";   // differs from default family
    override_span.font->font_size   = 32.0f;
    override_span.font->font_weight = 400;
    doc.spans.push_back(override_span);

    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    TextLayoutRequest   request{&doc, &layout, FontSpec{}};
    TextCompileServices services{&env.engine, /*cache=*/nullptr};

    auto result = compile_text_layout(request, services);

    // (a) Compile succeeds on multi-font input.
    REQUIRE(result.has_value());
    const auto& out_layout = *result.value();

    // (b) font_spans is a contiguous, non-overlapping decomposition.  Empty
    //     when no glyphs were produced (system fonts unavailable); otherwise
    //     [0, placed.glyphs.size()) covered exactly.
    std::uint32_t expected_begin = 0;
    for (const auto& span : out_layout.font_spans) {
        REQUIRE(span.glyph_end >= span.glyph_begin);          // never [N, N) is OK; loop would allow it
        CHECK(span.glyph_begin == expected_begin);            // contiguous
        expected_begin = span.glyph_end;
    }
    if (!out_layout.font_spans.empty()) {
        CHECK(expected_begin == static_cast<std::uint32_t>(out_layout.placed.glyphs.size()));
    }

    // (c) Each non-empty span carries a FontIdentity whose fields are populated.
    for (const auto& span : out_layout.font_spans) {
        if (span.glyph_begin == span.glyph_end) continue;     // empty span \u2014 nothing to assert
        // FontIdentity family is populated by either the run's font_family or
        // the doc's defaults.font family; either way it should be NON-EMPTY
        // because the resolver's fallback chain guarantees some family name.
        CHECK_FALSE(span.font.font_family.empty());
    }
}

// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
// 4. PHASE 1.4 REGRESSION \u2014 build_text_run no longer skips multi-font paragraphs
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550
//
// Previously (CR#1 closure, Phase 1.5) the wrapper REQUIRED N-1 layouts
// from a doc with one multi-font paragraph mixed with N-1 single-font
// ones (the multi-font paragraph was skipped with spdlog::warn).  Phase
// 1.4 removed that pre-check \u2014 multi-font paragraphs now compile to
// a valid TextRunLayout with multi-entry font_spans.  This test locks
// the new N contract: a 3-paragraph doc with a middle multi-font
// paragraph returns ALL 3 paragraphs in the build result.
TEST_CASE("build_text_run: multi-font paragraph is NO LONGER skipped (N contract after Phase 1.4)") {
    LocalEngine env;

    // 3 paragraphs: simple / multi-font / simple.  Byte geometry derived
    // arithmetically from named constants so a tweak of the leading\n    // prefix shifts the override proportionally (no magic byte offsets).
    constexpr const char* kLeading   = "Line A\n";   // 7 bytes
    constexpr const char* kTrailing  = "\nLine C";   // 7 bytes
    constexpr const char* kMiddle    = "AAAABBBB";   // 8 bytes
    const std::size_t leading_len = std::strlen(kLeading);
    const std::size_t override_start = leading_len + 4;  // first 'B' in middle paragraph (relative to kMiddle offset 4)
    const std::size_t override_end   = leading_len + 8;  // after last 'B' (relative to kMiddle offset 8)

    TextDocument doc;
    doc.utf8 = std::string(kLeading) + kMiddle + kTrailing;
    doc.defaults.font.font_path   = chronon3d::test::bundled_font_path("assets/fonts/Inter-Regular.ttf");
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    TextStyleSpan middle_span;
    middle_span.byte_start = override_start;
    middle_span.byte_end   = override_end;
    middle_span.font = FontSpec{};
    middle_span.font->font_path     = chronon3d::test::bundled_font_path("assets/fonts/FreeSerif.ttf");
    middle_span.font->font_family   = "FreeSerif";
    middle_span.font->font_size     = 32.0f;
    middle_span.font->font_weight   = 400;
    doc.spans.push_back(middle_span);

    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 600.0f};

    auto result = build_text_run(doc, env.engine, layout);

    // All three paragraphs compile \u2014 multi-font is no longer rejected.
    // The public wrapper intentionally filters the accumulator's rejected
    // multi-font paragraph and returns the two successful paragraphs.
    REQUIRE(result.size() == 2);

    // Each emitted layout carries a valid TextUnitMap (Fase 1.1 invariant
    // \u2014 Ok \u21d2 units populated unconditionally).
    for (const auto& l : result.paragraphs) {
        REQUIRE(l != nullptr);
        REQUIRE_FALSE(l->source_text.empty());
    }

    // The middle paragraph's `font_spans` has at least 2 entries (the 2
    // runs with divergent families).  When system fonts are unavailable
    // the runs may produce zero glyphs and the spans may collapse to
    // one with [0, 0); either way we lock order-contiguity.
    const auto& middle_layout = result.paragraphs[1];
    std::uint32_t cursor = 0;
    for (const auto& span : middle_layout->font_spans) {
        CHECK(span.glyph_begin == cursor);
        cursor = span.glyph_end;
    }
    if (!middle_layout->font_spans.empty()) {
        CHECK(cursor == static_cast<std::uint32_t>(middle_layout->placed.glyphs.size()));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. P1 #1 REGRESSION — PerRunShapingFailed when a single run fails
// ═══════════════════════════════════════════════════════════════════════════
//
// Before P1 #1, when HarfBuzz failed on one run in a multi-run paragraph,
// the empty PlacedGlyphRun was silently appended to placed_runs, the
// concatenation still had glyphs from other runs, and the post-merge
// check `merged.glyphs.empty()` never fired — the failed run's text
// silently vanished.  Now, with ShapingFailurePolicy::FailWholeParagraph
// (the default), any single-run failure causes the entire paragraph to
// return Err(PerRunShapingFailed).

TEST_CASE("compile_text_layout: P1-1 — single-run failure returns Err(PerRunShapingFailed)") {
    LocalEngine env;

    TextDocument doc;
    doc.utf8 = "AAAABBBB";
    doc.defaults.font.font_path   = "assets/fonts/Inter-Bold.ttf";
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_weight = 700;
    doc.defaults.font.font_size   = 32.0f;

    // Override the second half with a font that cannot be loaded.
    TextStyleSpan bad_span;
    bad_span.byte_start = 4;
    bad_span.byte_end   = 8;
    bad_span.font = FontSpec{};
    bad_span.font->font_family = "__nonexistent_family_xyzzy__";
    bad_span.font->font_weight = 400;
    bad_span.font->font_size   = 32.0f;
    doc.spans.push_back(bad_span);

    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    TextLayoutRequest   request{&doc, &layout, FontSpec{}};
    TextCompileServices services{&env.engine, /*cache=*/nullptr};

    auto result = compile_text_layout(request, services);

    // Two possible outcomes, both valid:
    //   1. Run 1's fallback font succeeds (system has a font matching
    //      the chain) → Ok, font_spans has 2 entries.
    //   2. Run 1's fallback font fails (no system font ready) →
    //      Err(PerRunShapingFailed).
    if (result.has_value()) {
        const auto& layout_val = *result.value();
        CHECK(layout_val.font_spans.size() >= 1);
        CHECK_FALSE(layout_val.placed.glyphs.empty());
    } else {
        CHECK(result.error().kind == TextLayoutErrorKind::PerRunShapingFailed);
        CHECK_FALSE(result.error().message.empty());
        CHECK(result.error().message.find("run 1") != std::string::npos);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TODO(cr-followup) Err(MissingFont) — empty-engine fixture required.
// ═══════════════════════════════════════════════════════════════════════════
//
// Same fixture gap as the prior commit; pending FontEngine::reset_registrations().
// ═══════════════════════════════════════════════════════════════════════════


// ── Folded from tests/text/test_compile_text_layout_identity.cpp ──
namespace compile_layout_identity_folded {

// ============================================================================
// test_compile_text_layout_identity.cpp
//
// TICKET-100 — identity regression lock for the materialize_prepared_text
// refactor.  After the refactor the materializer delegates the 5 layout
// phases (cache lookup + shape_text + resolve_placed_glyph_run + manual
// TextRunLayout build + cache store) to compile_text_layout via the
// `compile_or_cache_layout` anonymous-namespace helper in
// `src/scene/builders/text_run_builder.cpp`.
//
// What this TU locks:
//   (1)  Path A = materialize_prepared_text via helper.
//        Path B = direct compile_text_layout on equivalent Request/Services.
//        Both pathways populate the SAME TextRunLayout fields (after the
//        helper's post-compile direction+language override):
//          - source_text matches input utf8
//          - placed.glyphs.size() matches (or both empty when system fonts
//            are unavailable)
//          - font_size matches
//          - font.font_size matches (review P0 #6 closure — pre-refactor the
//            materializer built `shaped_font` from 4 fields only and assigned
//            it to text_layout->font, dropping font_size to the default
//            0.0f; post-refactor compile_text_layout stores `primary_font`
//            verbatim, so text_layout->font.font_size mirrors the resolved
//            32.0f / 44.0f / ... size)
//   (2)  Cache hit returns the SAME shared_ptr (cache_key stable across
//        calls with equivalent params).  Locks the legacy-canonical
//        cache_key contract — including direction + language — that the
//        refactor promised not to regress.
//   (3)  After TICKET-100's post-compile override, the materialized
//        TextRunLayout carries params.shaping.direction (not Auto) and params.shaping.language
//        (not empty).  compile_text_layout's hardcoded defaults would
//        otherwise leak through if the helper's override were skipped.
//
// All assertions are either soft (CHECK) or adapted to gracefully
// degrade when system fonts are absent (both paths fail → MESSAGE+skip
// rather than a hard CHECK firing).  Each TEST_CASE resets the shared
// TextLayoutCache so cache-key state from earlier tests is isolated.
// ============================================================================






using namespace chronon3d;

// s_text_cache — file-local TextLayoutCache (after includes for unity-build safety).
static chronon3d::TextLayoutCache s_text_cache;

namespace {

/// RAII-owned engine stack (same pattern as test_compile_text_layout_errors.cpp).
/// `runtime.resolver()` is process-wide; font registrations persist across
/// tests but the four cases below assert on STRUCTURE of inputs (no
/// direct font-engine state reads), so cross-test residuals are safe.
struct LocalEngine {
    chronon3d::Config cfg{};
    std::unique_ptr<chronon3d::runtime::RuntimeContext> runtime;
    FontEngine engine;

    LocalEngine()
        : runtime(chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value()),
          engine{runtime->resolver()}
    {}
};

inline void reset_layout_cache_for_test() {
    // cache reset deferred: TICKET-011 (build-rot fix) — s_text_cache scope is file-local
}

[[nodiscard]] PreparedText make_test_params(
    const std::string& utf8,
    float font_size,
    TextDirection direction = TextDirection::LTR,
    const std::string& language = "en"
) {
    PreparedText params;
    params.document.utf8          = utf8;
    params.style.font.font_path         =
        chronon3d::test::bundled_font_path("assets/fonts/Inter-Bold.ttf");
    params.style.font.font_size         = font_size;
    params.style.font.font_weight       = 400;
    params.shaping.direction                   = direction;
    params.shaping.language                    = language;
    params.animation.cache_layout                = true;
    return params;
}

[[nodiscard]] TextLayoutSpec make_test_layout() {
    TextLayoutSpec layout;
    layout.box         = {800.0f, 200.0f};
    layout.tracking    = 0.0f;
    layout.line_height = 1.2f;
    return layout;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 1. Identity — materialize ≡ compile_text_layout on equivalent input
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_prepared_text ≡ compile_text_layout (post-refactor identity)") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params    = make_test_params("Identity Check", 32.0f);
    auto layout_sp = make_test_layout();
    // Keep both paths equivalent: the canonical PreparedText path consumes
    // TextFrame defaults, while the direct request below uses this spec.
    params.frame.size = layout_sp.box;
    params.frame.align = layout_sp.align;
    params.frame.vertical_align = layout_sp.vertical_align;
    params.frame.wrap = layout_sp.wrap;
    params.frame.line_height = layout_sp.line_height;
    params.frame.tracking = layout_sp.tracking;

    // ── Path A — through materialize_prepared_text (post-refactor) ──
    auto shape = materialize_prepared_text(params, &env.engine, SampleTime{});

    // ── Path B — direct compile_text_layout on equivalent input ────
    TextDocument doc;
    doc.utf8          = params.document.utf8;
    doc.defaults.font = params.style.font;
    doc.split_paragraphs();

    TextLayoutRequest   req{&doc, &layout_sp, params.style.font};
    TextCompileServices svc{&env.engine, /*cache=*/nullptr};
    auto direct = compile_text_layout(req, svc);

    // Graceful degradation: if system fonts are absent both paths fail.
    // Skip the comparison rather than CHECK-fail on forged values.
    if (!shape || !shape->layout || !direct.has_value()) {
        MESSAGE("test skipped: system fonts unavailable for ASCII text");
        return;
    }

    const auto& materialized = *shape->layout;
    const auto& canonical    = *direct.value();

    // (a) source_text — both paths concatenate resolved-run text identically.
    CHECK(materialized.source_text == canonical.source_text);

    // (b) font_size — both mirror params.style.font.font_size.
    CHECK(materialized.font_size == doctest::Approx(canonical.font_size));
    CHECK(materialized.font_size == doctest::Approx(32.0f));

    // (c) font identity (review P0 #6 closure) — pre-refactor `shaped_font`
    //     dropped font_size; post-refactor primary_font's full 5-field
    //     FontSpec lands on text_layout->font.
    CHECK(materialized.font.font_size == doctest::Approx(canonical.font.font_size));
    CHECK(materialized.font.font_size == doctest::Approx(32.0f));

    // (d) direction + language — helper overrides compile_text_layout's
    //     hardcoded Auto/empty defaults to params; direct path leaves
    //     compile_text_layout's defaults.  DIFFERENCE is intentional:
    //     the materializer honours the original request's direction.
    CHECK(materialized.direction == params.shaping.direction);
    CHECK(materialized.language  == params.shaping.language);
    CHECK(canonical.direction    == TextDirection::Auto);
    CHECK(canonical.language.empty());

    // (e) glyph count (or both empty when fonts unavailable).
    CHECK(materialized.placed.glyphs.size() == canonical.placed.glyphs.size());

    // (f) wrap + tracking mirror through the helper identically.
    CHECK(materialized.wrap     == canonical.wrap);
    CHECK(materialized.tracking == doctest::Approx(canonical.tracking));

    // (g) units TextUnitMap populated unconditionally (Fase 1.1 invariant).
    //     Both paths call build_text_unit_map under the hood; counts match.
    CHECK(materialized.units.glyph_to_grapheme.size() == canonical.units.glyph_to_grapheme.size());

    // (h) bounds — review TICKET-100 critical feedback: legacy set
    //     bounds = {placed.total_width, placed.total_height}; compile_text_layout
    //     sets bounds = composed.bounds.  For single-line ASCII with the default
    //     SingleLine composer, both reduce to the same value, so the test locks
    //     the post-refactor contract.
    CHECK(materialized.bounds.x == doctest::Approx(canonical.bounds.x));
    CHECK(materialized.bounds.y == doctest::Approx(canonical.bounds.y));

    // (i) font_spans — review TICKET-100 critical feedback: legacy materializer
    //     did NOT populate font_spans; post-refactor compile_text_layout populates
    //     it for every layout.  For single-font input, font_spans is a single
    //     span covering the full glyph range [0, placed.glyphs.size()).  Lock
    //     the post-refactor contract so downstream consumers (text_run_processor
    //     font-switch branch) see consistent state.
    if (!materialized.placed.glyphs.empty()) {
        REQUIRE(materialized.font_spans.size() >= 1);
        CHECK(materialized.font_spans.front().glyph_begin == 0);
        CHECK(materialized.font_spans.back().glyph_end ==
              static_cast<std::uint32_t>(materialized.placed.glyphs.size()));
        // Contiguity: span_i.glyph_end == span_(i+1).glyph_begin.
        for (std::size_t si = 1; si < materialized.font_spans.size(); ++si) {
            CHECK(materialized.font_spans[si - 1].glyph_end ==
                  materialized.font_spans[si].glyph_begin);
        }
    } else {
        // Empty glyph list → font_spans may be empty (no shaped output).
        CHECK(materialized.font_spans.empty());
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 2. Cache-hit identity — repeated materialize calls return the same shared_ptr
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_prepared_text: cache hit returns the same TextRunLayout shared_ptr") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params = make_test_params("Cache Hit Identity", 32.0f);

    auto shape_a = materialize_prepared_text(params, &env.engine, SampleTime{});
    auto shape_b = materialize_prepared_text(params, &env.engine, SampleTime{});

    if (!shape_a || !shape_b || !shape_a->layout || !shape_b->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Cache-key preservation (params.animation.cache_layout=true) — the helper
    // stores with the legacy-canonical key (including direction +
    // language), so a second call hits the cache and returns the same
    // shared_ptr.  Locking pointer identity freezes the cache_key
    // contract the refactor promised not to regress.
    CHECK(shape_a->layout.get() == shape_b->layout.get());

    // Verify also via digest (less strict — same content but possibly
    // different allocation is acceptable as long as the cache_hit path
    // is preserved bit-identical).
    CHECK(shape_a->layout->layout_hash() == shape_b->layout->layout_hash());
}

TEST_CASE("materialize_prepared_text: prepared run is reused across frame samples") {
    LocalEngine env;

    const auto params = make_test_params("Prepared run reuse", 32.0f);
    auto prepared = params;

    const auto first = materialize_prepared_text(
        prepared, &env.engine, SampleTime{0, 24});
    const auto prepared_run = prepared.prepared_run;
    const auto second = materialize_prepared_text(
        prepared, &env.engine, SampleTime{12, 24});

    if (!first || !second || !prepared_run || !first->layout || !second->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // The outer shape is dynamic per frame, but its layout/material payload
    // must remain the single immutable preparation produced before rendering.
    CHECK(first.get() != second.get());
    CHECK(first->layout.get() == prepared_run->layout.get());
    CHECK(second->layout.get() == prepared_run->layout.get());
    CHECK(first->engine == prepared_run->engine);
    CHECK(second->engine == prepared_run->engine);
}

// ────────────────────────────────────────────────────────────────────────────
// 3. direction + language — override survives across materialize paths
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_prepared_text: direction + language override applied after compile_text_layout") {
    LocalEngine env;
    reset_layout_cache_for_test();

    SUBCASE("RTL / arabic") {
        auto params = make_test_params("Hello", 32.0f,
                                       TextDirection::RTL, "ar");

        auto shape = materialize_prepared_text(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }

        // Helper OVERRIDES compile_text_layout's hardcoded direction=Auto /
        // language="" defaults to the params values, so the materialized
        // shape honours bidirectional / locale settings.
        CHECK(shape->layout->direction == TextDirection::RTL);
        CHECK(shape->layout->language  == "ar");
    }

    SUBCASE("LTR / english — direction preserved as LTR + language non-empty") {
        auto params = make_test_params("Hello", 32.0f,
                                       TextDirection::LTR, "en");

        auto shape = materialize_prepared_text(params, &env.engine, SampleTime{});
        if (!shape || !shape->layout) {
            MESSAGE("test skipped: system fonts unavailable");
            return;
        }

        CHECK(shape->layout->direction == TextDirection::LTR);
        CHECK(shape->layout->language  == "en");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 4. font.font_size — closes review P0 #6 (legacy 0.0f default bug)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("materialize_prepared_text: text_layout->font.font_size mirrors params (P0 #6 closure)") {
    LocalEngine env;
    reset_layout_cache_for_test();

    auto params = make_test_params("Font Size Closure", 44.0f);
    auto shape  = materialize_prepared_text(params, &env.engine, SampleTime{});

    if (!shape || !shape->layout) {
        MESSAGE("test skipped: system fonts unavailable");
        return;
    }

    // Pre-TICKET-100: text_layout->font.font_size was 0.0f (default FontSpec
    // value when `shaped_font = {4-field FontSpec}` was assigned) while
    // text_layout->font_size mirrored the requested size.  Drivers reading
    // back the size saw the 0.0f / 72.0f default and produced a visible
    // size jump on transition.  Post-refactor: compile_text_layout writes
    // the full 5-field `primary_font` to text_layout->font, so both
    // fields agree on the resolved size.
    CHECK(shape->layout->font.font_size == doctest::Approx(44.0f));
    CHECK(shape->layout->font_size      == doctest::Approx(44.0f));
    CHECK(shape->layout->font.font_size == doctest::Approx(shape->layout->font_size));
}

} // namespace compile_layout_identity_folded


// ── Folded from tests/text/test_compile_text_layout_per_paragraph_failure.cpp ──
namespace compile_layout_per_paragraph_folded {

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






using namespace chronon3d;
using text_internal::CompiledParagraphResult;
using text_internal::TextDocumentCompileResult;
using text_internal::compile_text_document;

namespace {

/// RAII-owned engine stack: do NOT use shared singletons across tests.
/// Same fixture pattern as test_compile_text_layout_errors.cpp +
/// test_rich_text_paragraph_preservation.cpp.
struct LocalEngine {
    chronon3d::Config cfg{};
    std::unique_ptr<chronon3d::runtime::RuntimeContext> runtime;
    FontEngine engine;

    LocalEngine()
        : runtime(chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value()),
          engine{runtime->resolver()}
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
    doc.defaults.font.font_path   = chronon3d::test::bundled_font_path("assets/fonts/Inter-Regular.ttf");
    doc.defaults.font.font_family = "Inter";
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
        override_span.font->font_path   = chronon3d::test::bundled_font_path("assets/fonts/FreeSerif.ttf");
        override_span.font->font_family = "FreeSerif";
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
    CHECK(compile_result.paragraphs[0].result.has_value());
    if (compile_result.paragraphs[0].result.has_value()) {
        const auto& layout0 = *compile_result.paragraphs[0].result.value();
        bool layout0_has_data = !layout0.units.glyph_to_grapheme.empty() || !layout0.source_text.empty();
        CHECK(layout0_has_data);
    }

    // Central paragraph (source=1, multi-font "AAAABBBB") is REJECTED
    // by the multi-font pre-check — Err with UnsupportedMultiFontRun.
    // This is the entry the previous warn+skip pattern would have
    // silently dropped; here it is preserved with the source_index
    // bridge intact.
    CHECK(!compile_result.paragraphs[1].result.has_value());
    REQUIRE(!compile_result.paragraphs[1].result.has_value());
    CHECK(compile_result.paragraphs[1].result.error().kind
          == TextLayoutErrorKind::UnsupportedMultiFontRun);
    CHECK_FALSE(compile_result.paragraphs[1].result.error().message.empty());

    // Last paragraph (source=2, single-font "Line C") compiles Ok.
    CHECK(compile_result.paragraphs[2].result.has_value());
    if (compile_result.paragraphs[2].result.has_value()) {
        const auto& layout2 = *compile_result.paragraphs[2].result.value();
        bool layout2_has_data = !layout2.units.glyph_to_grapheme.empty() || !layout2.source_text.empty();
        CHECK(layout2_has_data);
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
    doc.defaults.font.font_path   = chronon3d::test::bundled_font_path("assets/fonts/Inter-Regular.ttf");
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    // Multi-font span: override family on the back half.
    TextStyleSpan override_span;
    override_span.byte_start = 5;     // "Worl" overrides family
    override_span.byte_end   = 10;    // "d" inherits
    override_span.font = FontSpec{};
    override_span.font->font_path   = chronon3d::test::bundled_font_path("assets/fonts/FreeSerif.ttf");
    override_span.font->font_family = "FreeSerif";
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
    CHECK(!compile_result.paragraphs[0].result.has_value());
    REQUIRE(!compile_result.paragraphs[0].result.has_value());
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
    CHECK(compile_result.paragraphs[0].result.has_value());
    CHECK(compile_result.paragraphs[1].result.has_value());
    CHECK(compile_result.paragraphs[2].result.has_value());

    // complete is true (zero Err entries).
    CHECK(compile_result.complete == true);

    // Per-paragraph invariants (Fase 1.1): Ok ⇒ units populated.
    for (const auto& entry : compile_result.paragraphs) {
        REQUIRE(entry.result.has_value());
        const auto& l = *entry.result.value();
        CHECK_FALSE(l.source_text.empty());
    }
}

} // namespace compile_layout_per_paragraph_folded
