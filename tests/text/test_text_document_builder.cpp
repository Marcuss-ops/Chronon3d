// ═══════════════════════════════════════════════════════════════════════════
// test_text_document_builder.cpp — FASE 4a Builder pattern tests
// ═══════════════════════════════════════════════════════════════════════════

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/text/text_document_builder.hpp>

using namespace chronon3d;

TEST_CASE("TextDocumentBuilder: empty document produces valid utf8") {
    auto doc = TextDocumentBuilder().build();
    CHECK(doc.utf8.empty());
    CHECK(doc.spans.empty());
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: single span with no overrides") {
    auto doc = TextDocumentBuilder()
        .span("Hello, World!")
        .build();

    CHECK(doc.utf8 == "Hello, World!");
    // No overrides → no TextStyleSpan entries.
    CHECK(doc.spans.empty());
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: single span with color") {
    auto doc = TextDocumentBuilder()
        .span("Red text")
            .color(1.0f, 0.0f, 0.0f, 1.0f)
        .build();

    CHECK(doc.utf8 == "Red text");
    REQUIRE(doc.spans.size() == 1);
    CHECK(doc.spans[0].byte_start == 0);
    CHECK(doc.spans[0].byte_end == 8);
    REQUIRE(doc.spans[0].appearance.has_value());
    CHECK(doc.spans[0].appearance->color.r == doctest::Approx(1.0f));
    CHECK(doc.spans[0].appearance->color.g == doctest::Approx(0.0f));
    CHECK(doc.spans[0].appearance->color.b == doctest::Approx(0.0f));
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: single span with weight") {
    auto doc = TextDocumentBuilder()
        .span("Bold")
            .weight(700)
        .build();

    CHECK(doc.utf8 == "Bold");
    REQUIRE(doc.spans.size() == 1);
    REQUIRE(doc.spans[0].font.has_value());
    CHECK(doc.spans[0].font->font_weight == 700);
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: single span with scale") {
    auto doc = TextDocumentBuilder()
        .span("Big text")
            .scale(1.5f)
        .build();

    CHECK(doc.utf8 == "Big text");
    REQUIRE(doc.spans.size() == 1);
    // FASE 4a: scale stored as font_size_multiplier (not absolute px).
    REQUIRE(doc.spans[0].font_size_multiplier.has_value());
    CHECK(doc.spans[0].font_size_multiplier.value() == doctest::Approx(1.5f));
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: single span with semantic_id") {
    auto doc = TextDocumentBuilder()
        .span("keyword")
            .semantic_id("kw-1")
        .build();

    CHECK(doc.utf8 == "keyword");
    REQUIRE(doc.spans.size() == 1);
    // FASE 4a: semantic_id is now wired into TextStyleSpan.
    REQUIRE(doc.spans[0].semantic_id.has_value());
    CHECK(doc.spans[0].semantic_id.value() == "kw-1");
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: multiple spans with different styles") {
    auto doc = TextDocumentBuilder()
        .span("Hello")
            .color(1.0f, 0.0f, 0.0f, 1.0f)
            .weight(700)
        .span(" ")
        .span("World")
            .color(0.0f, 0.0f, 1.0f, 1.0f)
            .scale(1.2f)
        .build();

    CHECK(doc.utf8 == "Hello World");
    REQUIRE(doc.spans.size() == 2);  // space span has no overrides → not in spans

    // "Hello" span
    CHECK(doc.spans[0].byte_start == 0);
    CHECK(doc.spans[0].byte_end == 5);
    REQUIRE(doc.spans[0].appearance.has_value());
    CHECK(doc.spans[0].appearance->color.r == doctest::Approx(1.0f));
    CHECK(doc.spans[0].font.has_value());
    CHECK(doc.spans[0].font->font_weight == 700);

    // "World" span
    CHECK(doc.spans[1].byte_start == 6);
    CHECK(doc.spans[1].byte_end == 11);
    REQUIRE(doc.spans[1].appearance.has_value());
    CHECK(doc.spans[1].appearance->color.b == doctest::Approx(1.0f));
    REQUIRE(doc.spans[1].font_size_multiplier.has_value());
    CHECK(doc.spans[1].font_size_multiplier.value() == doctest::Approx(1.2f));

    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: defaults sets document-level TextSpec") {
    TextSpec defaults;
    defaults.font.font_size = 48.0f;
    defaults.appearance.color = Color{0.5f, 0.5f, 0.5f, 1.0f};

    auto doc = TextDocumentBuilder()
        .defaults(defaults)
        .span("Styled")
        .build();

    CHECK(doc.defaults.font.font_size == doctest::Approx(48.0f));
    CHECK(doc.defaults.appearance.color.r == doctest::Approx(0.5f));
    CHECK(doc.utf8 == "Styled");
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: baseline_shift override") {
    auto doc = TextDocumentBuilder()
        .span("superscript")
            .baseline_shift(4.0f)
        .build();

    CHECK(doc.utf8 == "superscript");
    REQUIRE(doc.spans.size() == 1);
    CHECK(doc.spans[0].baseline_shift.has_value());
    CHECK(doc.spans[0].baseline_shift.value() == doctest::Approx(4.0f));
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: tracking override") {
    auto doc = TextDocumentBuilder()
        .span("spaced")
            .tracking(2.5f)
        .build();

    CHECK(doc.utf8 == "spaced");
    REQUIRE(doc.spans.size() == 1);
    CHECK(doc.spans[0].tracking.has_value());
    CHECK(doc.spans[0].tracking.value() == doctest::Approx(2.5f));
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: chained calls without span are no-ops") {
    // Calling overrides without a preceding .span() should not crash.
    auto doc = TextDocumentBuilder()
        .color(1.0f, 0.0f, 0.0f)
        .weight(700)
        .scale(1.5f)
        .semantic_id("orphan")
        .tracking(1.0f)
        .baseline_shift(2.0f)
        .span("Real span")
        .build();

    CHECK(doc.utf8 == "Real span");
    CHECK(doc.spans.empty());  // no overrides on the actual span
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: build_copy does not consume builder") {
    TextDocumentBuilder builder;
    builder.span("Copy test").color(1.0f, 0.0f, 0.0f);

    auto doc1 = builder.build_copy();
    CHECK(doc1.utf8 == "Copy test");
    CHECK(doc1.spans.size() == 1);

    // Builder still usable after build_copy.
    auto doc2 = builder.build_copy();
    CHECK(doc2.utf8 == "Copy test");
    CHECK(doc2.spans.size() == 1);
}

TEST_CASE("TextDocumentBuilder: scale rejects non-positive values") {
    auto doc = TextDocumentBuilder()
        .span("Normal")
            .scale(0.0f)
        .build();

    CHECK(doc.utf8 == "Normal");
    // scale=0.0 is rejected → no font override created.
    CHECK(doc.spans.empty());
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: weight clamped to 100–900") {
    auto doc = TextDocumentBuilder()
        .span("ExtraBold")
            .weight(1200)
        .build();

    REQUIRE(doc.spans.size() == 1);
    REQUIRE(doc.spans[0].font.has_value());
    CHECK(doc.spans[0].font->font_weight == 900);  // clamped
    CHECK(doc.validate());
}

TEST_CASE("TextDocumentBuilder: paragraph auto-population") {
    auto doc = TextDocumentBuilder()
        .span("Line one")
        .build();

    CHECK(doc.utf8 == "Line one");
    // split_paragraphs should create at least one paragraph.
    CHECK_FALSE(doc.paragraphs.empty());
    CHECK(doc.validate());
}
