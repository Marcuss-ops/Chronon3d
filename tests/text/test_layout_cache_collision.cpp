// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_layout_cache_collision.cpp
//
// TICKET-103a -- text cat-3 #4 cache-key collision regression suite.
//
// Locks the contract from the TICKET-103a extension of TextLayoutRequest
// with `direction`, `language`, `features` (Bcp47LanguageTag /
// TextShapingFeatures aliases of std::string in `include/chronon3d/text/
// text_run.hpp, see that header for the rationale).  ALL 3 fields are now
// part of the cache-key signature, and the previous force-overrides in
// `build_cache_key` at src/text/text_run_builder.cpp are GONE.
//
// 2 deterministic TEST_CASEs (no threads, no time, no PRNG — AGENTS.md
// v0.1 cat-2 freeze-compliant):
//
//   (1) Same text + same font + same wrap + same layout, but
//       direction=LTR vs direction=RTL → cache_key.digest() MUST differ.
//       (Locks the bidi cache-collision contract: under TICKET-103a's
//       refactor, the cache_key correctly separates LTR/RTL inputs into
//       distinct entries; pre-TICKET-103a, the per-run-bidi override
//       collapsed them all to TextDirection::Auto, causing a bidirectional
//       shape cache hit on a Latin-only entry — a known regression.)
//
//   (2) Same text + same font + same wrap + same direction, but
//       language="ar" vs language="en" → cache_key.digest() MUST differ.
//       (Locks the BCP-47 cache-collision contract: under TICKET-103a's
//       refactor, the cache_key correctly separates Arabic vs English
//       inputs (necessary because HarfBuzz's script-specific shaping
//       decisions depend on language); pre-TICKET-103a, language was
//       `.clear()`-ed, collapsing them — same false-hit risk.)
//
// No font engine / system-font dependency: these are PURE
// TextLayoutCacheKey digest checks.  The test runs even when system
// fonts are absent, unlike the materialize/compile paths exercised in
// test_compile_text_layout_identity.cpp / test_rich_text_paragraph_
// preservation.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>  // TextLayoutCacheKey, digest(), Bcp47LanguageTag

#include <doctest/doctest.h>

#include <cstdint>  // std::uint64_t

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (1) — direction collision (same text + font, LTR vs RTL)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-103a (1) TextLayoutCacheKey: same text+font but direction LTR vs RTL → different digest") {
    // Identical inputs across the 8 non-direction/language fields.  Only
    // direction differs.  Pre-TICKET-103a, build_cache_key at
    // src/text/text_run_builder.cpp forced both to
    // TextDirection::Auto + language.clear() → identical digests (false
    // hit on bidirectional shaping).  Post-TICKET-103a, the cache-key
    // signature honors direction → distinct digests.
    TextLayoutCacheKey ltr_key{
        .text          = "Hello, World",
        .font_path     = "/fonts/Inter-Regular.ttf",
        .font_weight   = 400,
        .font_style    = "normal",
        .font_size     = 32.0f,
        .tracking      = 0.0f,
        .box_width     = 800.0f,
        .wrap          = TextWrap::None,
        .direction     = TextDirection::LTR,
        .language      = Bcp47LanguageTag{"en"},
        .features      = TextShapingFeatures{},
        .font_family   = "",
        .paragraph     = ParagraphStyle{},
    };
    TextLayoutCacheKey rtl_key = ltr_key;
    rtl_key.direction = TextDirection::RTL;

    // Sanity check: the only differing field is direction.
    REQUIRE(ltr_key.text          == rtl_key.text);
    REQUIRE(ltr_key.font_path     == rtl_key.font_path);
    REQUIRE(ltr_key.font_weight   == rtl_key.font_weight);
    REQUIRE(ltr_key.font_style    == rtl_key.font_style);
    REQUIRE(ltr_key.font_size     == rtl_key.font_size);
    REQUIRE(ltr_key.tracking      == rtl_key.tracking);
    REQUIRE(ltr_key.box_width     == rtl_key.box_width);
    REQUIRE(ltr_key.wrap          == rtl_key.wrap);
    REQUIRE(ltr_key.language      == rtl_key.language);
    REQUIRE(ltr_key.features      == rtl_key.features);
    REQUIRE(ltr_key.paragraph     == rtl_key.paragraph);
    CHECK(ltr_key.direction       != rtl_key.direction);

    // Primary assertion: digests MUST differ.  This is the collision
    // contract: an LTR-rendered "Hello, World" and an RTL-rendered
    // "Hello, World" must NOT share the same cache slot, because the
    // expected glyph placements diverge (HarfBuzz bidi reorder).
    CHECK(ltr_key.digest() != rtl_key.digest());

    // Inverse check: identical direction produces identical digest
    // (sanity against premature optimisation that over-hashes distinct
    // runs as the same).
    TextLayoutCacheKey ltr_key_duplicate = ltr_key;
    CHECK(ltr_key.digest() == ltr_key_duplicate.digest());
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (2) — language collision (same text + font, ar vs en)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-103a (2) TextLayoutCacheKey: same text+font but language ar vs en → different digest") {
    // Identical inputs across the 8 non-direction/language fields.  Only
    // language differs.  Pre-TICKET-103a, build_cache_key at
    // src/text/text_run_builder.cpp cleared language → identical
    // digests (false hit on Arabic vs English shaping — HarfBuzz's
    // script-specific decisions depend on language).  Post-TICKET-103a,
    // the cache-key signature honors language → distinct digests.
    TextLayoutCacheKey en_key{
        .text          = "Hello, World",
        .font_path     = "/fonts/Inter-Regular.ttf",
        .font_weight   = 400,
        .font_style    = "normal",
        .font_size     = 32.0f,
        .tracking      = 0.0f,
        .box_width     = 800.0f,
        .wrap          = TextWrap::None,
        .direction     = TextDirection::LTR,
        .language      = Bcp47LanguageTag{"en"},
        .features      = TextShapingFeatures{},
        .font_family   = "",
        .paragraph     = ParagraphStyle{},
    };
    TextLayoutCacheKey ar_key = en_key;
    ar_key.language = Bcp47LanguageTag{"ar"};

    // Sanity check: the only differing field is language.
    REQUIRE(en_key.text          == ar_key.text);
    REQUIRE(en_key.font_path     == ar_key.font_path);
    REQUIRE(en_key.font_weight   == ar_key.font_weight);
    REQUIRE(en_key.font_style    == ar_key.font_style);
    REQUIRE(en_key.font_size     == ar_key.font_size);
    REQUIRE(en_key.tracking      == ar_key.tracking);
    REQUIRE(en_key.box_width     == ar_key.box_width);
    REQUIRE(en_key.wrap          == ar_key.wrap);
    REQUIRE(en_key.direction     == ar_key.direction);
    REQUIRE(en_key.features      == ar_key.features);
    REQUIRE(en_key.paragraph     == ar_key.paragraph);
    CHECK(en_key.language        != ar_key.language);

    // Primary assertion: digests MUST differ.  HarfBuzz forms
    // different glyph clusters for Arabic vs English (e.g. "Hello"
    // stays Latin-no-shaping-needed in language=en, but Arabic requires
    // initial/medial/final form selection in language=ar — the cache
    // must NOT alias them).
    CHECK(en_key.digest() != ar_key.digest());

    // Inverse check: identical language produces identical digest.
    TextLayoutCacheKey en_key_duplicate = en_key;
    CHECK(en_key.digest() == en_key_duplicate.digest());
}
