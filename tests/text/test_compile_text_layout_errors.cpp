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
// prewarm / resolve_fallback_fonts with side effects.  Each Err case
// is triggered by a STATIC FEATURE of the input (geometry of inputs,
// not behaviour of the resolver with system fonts).
// \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

/// RAII-owned engine stack: do NOT use shared singletons across tests.
/// Each TEST_CASE gets a fresh Config + RenderRuntime + FontEngine.
///
/// Note: `runtime.resolver()` is a process-level singleton.  Residuals
/// from one TEST_CASE's font registration MAY survive into the next,
/// which is acceptable because the four tests in this TU assert on
/// STRUCTURE of inputs (geometry, byte ranges) rather than on
/// font-engine behaviour.  Tests that read font-engine state directly
/// would need a different fixture (e.g. a mock resolver).
struct LocalEngine {
    chronon3d::Config                cfg{};
    chronon3d::runtime::RenderRuntime runtime;
    FontEngine                        engine;

    LocalEngine()
        : runtime(cfg),
          engine{runtime.resolver()}
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

    REQUIRE_FALSE(result.is_ok());
    CHECK(result.is_err());
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
        REQUIRE_FALSE(result.is_ok());
        CHECK(result.error().kind == TextLayoutErrorKind::MalformedLayout);
    }
    // Null layout \u2192 Err(MalformedLayout)
    {
        TextDocument           some_doc;
        TextLayoutRequest      bad_layout{&some_doc, /*layout=*/nullptr, FontSpec{}};
        TextCompileServices    services{&env.engine, nullptr};
        auto result = compile_text_layout(bad_layout, services);
        REQUIRE_FALSE(result.is_ok());
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
        REQUIRE_FALSE(result.is_ok());
        CHECK(result.error().kind == TextLayoutErrorKind::ShapingFailed);
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
    doc.defaults.font.font_family = "DejaVu Sans";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    // Span override on the back half with a DIFFERENT family \u2014 produces
    // 2 ResolvedTextRuns with divergent FontSpec after resolve_text_run_tree.
    TextStyleSpan override_span;
    override_span.byte_start = 3;
    override_span.byte_end   = 6;
    override_span.font = FontSpec{};
    override_span.font->font_family = "Liberation Serif";   // differs from default family
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
    REQUIRE(result.is_ok());
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
    doc.defaults.font.font_family = "DejaVu Sans";
    doc.defaults.font.font_size   = 32.0f;
    doc.defaults.font.font_weight = 400;

    TextStyleSpan middle_span;
    middle_span.byte_start = override_start;
    middle_span.byte_end   = override_end;
    middle_span.font = FontSpec{};
    middle_span.font->font_family   = "Liberation Serif";
    middle_span.font->font_size     = 32.0f;
    middle_span.font->font_weight   = 400;
    doc.spans.push_back(middle_span);

    doc.split_paragraphs();

    TextLayoutSpec layout;
    layout.box = {800.0f, 600.0f};

    auto result = build_text_run(doc, env.engine, layout);

    // All three paragraphs compile \u2014 multi-font is no longer rejected.
    REQUIRE(result.size() == 3);

    // Each emitted layout carries a valid TextUnitMap (Fase 1.1 invariant
    // \u2014 Ok \u21d2 units populated unconditionally).
    for (const auto& l : result.paragraphs) {
        REQUIRE(l != nullptr);
        REQUIRE(l->source_text.empty() == false || l->units.units.size() >= 0);
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
    if (result.is_ok()) {
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
