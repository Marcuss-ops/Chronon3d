// ═══════════════════════════════════════════════════════════════════════════
// test_text_resolver.cpp — TextDocument resolution tests
//
// PR 6 covers:
//   1. Empty document → empty tree
//   2. Single paragraph, no spans → single LTR run with default font
//   3. TextStyleSpan overrides → font changes create separate runs
//   4. Bidi segmentation → mixed LTR/RTL text produces directional runs
//   5. Font fallback → missing primary font falls back to generic
//   6. Multiple paragraphs → correct paragraph boundaries
//   7. Determinism
// ═══════════════════════════════════════════════════════════════════════════

#include <optional>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

// ── Helper: make a TextDocument with given utf8 + paragraphs ──────────────

namespace {

TextDocument make_doc(const std::string& utf8) {
    TextDocument doc;
    doc.utf8 = utf8;
    doc.defaults.font.font_family = "DefaultFamily";
    doc.defaults.font.font_size   = 72.0f;
    doc.split_paragraphs();
    return doc;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Empty document
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: empty document returns empty tree") {
    TextDocument doc;
    // Don't call split_paragraphs — the resolver will return empty tree
    // for documents without paragraphs.
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    CHECK(tree.paragraphs.empty());
    CHECK(tree.total_runs() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Single paragraph, no spans
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: single paragraph produces singl    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value(););
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() == 1);
    REQUIRE(tree.paragraphs[0].runs.size() == 1);

    const auto& run = tree.paragraphs[0].runs[0];
    CHECK(run.text == "Hello world");
    CHECK(run.direction == TextDirection::LTR);
    CHECK(run.byte_offset == 0);
    CHECK(run.byte_len == doc.utf8.size());
    CHECK(run.font.font_family == "DefaultFamily");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. TextStyleSpan overrides → font changes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: font-override span creates separate run") {
    TextDocument doc;
    doc.utf8 = "AAABBB";
    doc.defaults.font.font_family = "DefaultFamily";
    doc.defaults.font.font_size   = 72.0f;

    // Override bytes [3, 6) ("BBB") with a different font family.
    TextStyleSpan span;
    span.byte_start = 3;
    span.byte_end   = 6;
     auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();ly = "OverrideFamily";
    doc.spans.push_back(span);

    doc.split_paragraphs();

    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);

    REQUIRE(tree.paragraphs.size() == 1);
    // Two runs expected: [0,3) with DefaultFamily, [3,6) with OverrideFamily.
    REQUIRE(tree.paragraphs[0].runs.size() == 2);

    CHECK(tree.paragraphs[0].runs[0].text == "AAA");
    CHECK(tree.paragraphs[0].runs[0].font.font_family == "DefaultFamily");
    CHECK(tree.paragraphs[0].runs[0].byte_offset == 0);
    CHECK(tree.paragraphs[0].runs[0].byte_len == 3);

    CHECK(tree.paragraphs[0].runs[1].text == "BBB");
    CHECK(tree.paragraphs[0].runs[1].font.font_family == "OverrideFamily");
    CHECK(tree.paragraphs[0].runs[1].byte_offset == 3);
    CHECK(tree.paragraphs[0].runs[1].byte_len == 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Bidi segmentation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: mixed LTR/RTL text produces directional runs") {
       auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value(); (LTR)
    // The Arabic word is U+0633 U+0644 U+0627 U+0645
    std::string mixed = "Hello \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 World";
    auto doc = make_doc(mixed);
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() == 1);

    // When FriBidi is available, we expect 3 runs (LTR, RTL, LTR).
    // When FriBidi is not available, only 1 run (all LTR).
    size_t run_count = tree.paragraphs[0].runs.size();
    CHECK(run_count >= 1);

#if defined(CHRONON3D_HAS_FRIBIDI)
    CHECK(run_count == 3);
    CHECK(tree.paragraphs[0].runs[0].direction == TextDirection::LTR);
    CHECK(tree.paragraphs[0].runs[1].direction == TextDirection::RTL);
    CHECK(tree.paragraphs[0].runs[2].direction == TextDirection::LTR);

    // Byte offsets must be contiguous and cover the full text.
    size_t total_bytes = 0;
    for (const auto& r : tree.paragraphs[0].runs) {
        total_bytes += r.byte_len;
    }
    CHECK(total_bytes == doc.utf8.size());
#else
    // Without FriBidi: single LTR run containing the full text.
    CHECK(run_count == 1);
    CHECK(tree.paragraphs[0].runs[0].direction == TextDirection::LTR);
    CHECK(tree.paragraphs[0].runs[0].by    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();═════════════════════════════════════════════════════════════════
// 5. Font fallback
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: resolve_fallback_fonts returns primary if loadable") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    FontSpec spec;
    spec.font_family = "DejaVu Sans";

    // If DejaVu Sans is available on the system, it should return unchanged.
    // If not, it falls through to next candidates.  We just check that
    // the function doesn't crash and returns a FontSpec.
    auto resolved = resolve_fallback_fonts(spec, engine);
    // The family should be unchanged because the primary was loadable
    // or the fallback chain retur    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();mily.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Multiple paragraphs
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: newline creates separate paragraphs") {
    auto doc = make_doc("Line one\nLine two\nLine three");
    chronon3d::Config cfg;
    chronon3d::runtime::Rende    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();e.resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() == 3);

    CHECK(tree.paragraphs[0].runs[0].text == "Line one");
    CHECK(tree.paragraphs[1].runs[0].text == "Line two");
    CHECK(tree.paragraphs[2].runs[0].text == "Line three");
}

TEST_CASE("TextResolver: consecutive newlines produce empty paragraph") {
    auto doc = make_doc("Para A\n\nPara C");
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() == 3);

    // Middle paragraph is empty (two consecutive \n).
    CHECK(tree.paragraphs[0].runs[0].text == "Para A");
    // Empty paragraph has no runs.
    CHECK(tree.paragraphs[1].runs.empty());
    CHECK(tree.paragraphs[2].runs[0].text == "Para C");
}

// ═══════════════════════════════════════    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();h style propagation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: paragraph style propagates to runs") {
    TextDocument doc;
    doc.utf8 = "Styled text";
    doc.defaults.font.font_family = "Test";

    ParagraphRange pr;
    pr.byte_start = 0;
    pr.byte_end   = doc.utf8.size();
    pr.style.justification = TextJustification::Center;
    pr.style.composer = ParagraphComposer::EveryLine;
    doc.paragraphs.push_back(pr);

    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() ==    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();1);

    CHECK(tree.paragraphs[0].style.justification == TextJustification::Center);
    CHECK(tree.paragraphs[0].style.composer == ParagraphComposer::EveryLine);
    CHECK(tree.paragraphs[0].runs[0].paragraph_style.justification
          == TextJustification::Center);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResolver: same input produces same output") {
    auto doc = make_doc("Hello world\nSecond paragraph");
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto a = resolve_text_run_tree(doc, engine);
    auto b = resolve_text_run_tree(doc, engine);

    REQUIRE(a.paragraphs.size() == b.paragraphs.size());
    for (size_t i = 0; i < a.paragraphs.size(); ++i) {
        REQUIRE(a.paragraphs[i].runs.size() == b.paragraphs[i].runs.size());
        for (size_t j = 0; j < a.paragraphs[i].runs.size(); ++j) {
            CHECK(a.paragraphs[i].runs[j].text == b.paragraphs[i].runs[j].text);
            CHECK(a.paragraphs[i].runs[j].direction == b.paragraphs[i].runs[j].direction);
            CHECK(a.paragraphs[i].runs[j].byte_offset == b.paragraphs[i].runs[j].byte_offset);
            CHECK(a.paragraphs[i].runs[j].byte_    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    }
}


// ============================================================================
// TICKET-101 follow-up — BCP-47 source-canonicalization regression
// ============================================================================

TEST_CASE("TextResolver: BCP-47 language tag is canonicalized at source (TICKET-101)") {
    TextDocument doc;
    doc.utf8 = "Hello";
    doc.defaults.font.font_family = "Inter";
    doc.defaults.font.font_size   = 72.0f;

    ParagraphRange pr;
    pr.byte_start = 0;
    pr.byte_end   = doc.utf8.size();
    pr.style.language = "EN-US";  // mixed case — must be lowercased
    doc.paragraphs.push_back(pr);

    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    auto tree = resolve_text_run_tree(doc, engine);
    REQUIRE(tree.paragraphs.size() == 1);
    REQUIRE(tree.paragraphs[0].runs.size() == 1);
    // TICKET-101 follow-up: canonicalize_bcp47_language_tag lowercases
    // at the SOURCE in resolve_text_run_tree (MAJOR #1 fix).  Now
    // BOTH the paragraph style and the per-run language carry the
    // canonical form, so all downstream consumers see the same value.
    CHECK(tree.paragraphs[0].style.language == "en-us");
}
