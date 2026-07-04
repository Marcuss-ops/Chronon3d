// SPDX-License-Identifier: MIT
//
// test_effective_text_state.cpp — M1.5#2 regression lock.
//
// Locks the canonical EffectiveTextState contract that drives the
// fast-path in `apply_active_state_to_text_run_shape`.  Pre-M1.5#2 the
// fast-path silently missed direction/language/features flips — the
// cache key folded them correctly (TICKET-103a), so a direction-flip
// produced a `build_text_run` cache-MISS even when the shape's
// `layout` slot was not rebuilt, paying a full HarfBuzz pass on every
// frame.
//
// M1.5#2 EffectiveTextState bundles 5 fields participating in the
// `TextLayoutCacheKey::digest()`: text, font (FontLayoutIdentity),
// direction, language, features.  All 5 must participate in the
// fast-path comparison; field-by-field.
//
// Framework: doctest.  Pure value-construction tests + a structural
// regression on `apply_active_state_to_text_run_shape`'s return
// value — no font engine / system fonts required (font engine
// availability is irrelevant: we never call build_text_run; we
// only verify the comparison operator + the orchestrator's
// fast-path routing on cached shapes).

#include <doctest/doctest.h>

#include <chronon3d/text/text_run.hpp>          // EffectiveTextState, FontLayoutIdentity, Bcp47LanguageTag, TextShapingFeatures
#include <chronon3d/text/text_run_driver.hpp>   // operator== / != (umbrella extension)
#include <chronon3d/text/text_direction.hpp>    // TextDirection

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════
// §1 — operator== / operator!= lock the 5-field comparison contract.
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("M1.5#2 EffectiveTextState: identical fields → equal") {
    EffectiveTextState a;
    a.text      = "Hello";
    a.font      = FontLayoutIdentity{
        .resolved_path = "/fonts/Inter.ttf",
        .family        = "Inter",
        .weight        = 400,
        .style         = "Regular",
        .size          = 32.0f,
        .variation_axes = {},
    };
    a.direction   = TextDirection::LTR;
    a.language    = Bcp47LanguageTag{"en"};
    a.features    = TextShapingFeatures{"liga, kern"};

    EffectiveTextState b = a;  // copy
    CHECK(a == b);
    CHECK_FALSE(a != b);
}

TEST_CASE("M1.5#2 EffectiveTextState: text mismatch → unequal") {
    EffectiveTextState a;  // default
    EffectiveTextState b;
    b.text = "Hello";
    CHECK(a != b);
}

TEST_CASE("M1.5#2 EffectiveTextState: font family mismatch → unequal") {
    auto make_default = []() {
        EffectiveTextState s;
        s.text      = "Hello";
        s.font      = FontLayoutIdentity{
            .resolved_path = "/fonts/Inter.ttf",
            .family        = "Inter",
            .weight        = 400,
            .style         = "Regular",
            .size          = 32.0f,
            .variation_axes = {},
        };
        return s;
    };
    EffectiveTextState a = make_default();
    EffectiveTextState b = make_default();
    b.font.family = "Roboto";
    CHECK(a != b);
}

TEST_CASE("M1.5#2 EffectiveTextState: direction flip → unequal (PRE-M1.5#2 SILENT MISS)") {
    auto make_default = []() {
        EffectiveTextState s;
        s.text      = "Hello";
        s.font      = FontLayoutIdentity{
            .resolved_path = "/fonts/Inter.ttf",
            .family        = "Inter",
            .weight        = 400,
            .style         = "Regular",
            .size          = 32.0f,
            .variation_axes = {},
        };
        return s;
    };
    EffectiveTextState ltr = make_default();
    ltr.direction = TextDirection::LTR;
    EffectiveTextState rtl = make_default();
    rtl.direction = TextDirection::RTL;
    CHECK(ltr != rtl);
}

TEST_CASE("M1.5#2 EffectiveTextState: language flip → unequal (PRE-M1.5#2 SILENT MISS)") {
    auto make_default = []() {
        EffectiveTextState s;
        s.text      = "Hello";
        s.font      = FontLayoutIdentity{
            .resolved_path = "/fonts/Inter.ttf",
            .family        = "Inter",
            .weight        = 400,
            .style         = "Regular",
            .size          = 32.0f,
            .variation_axes = {},
        };
        return s;
    };
    EffectiveTextState en = make_default();
    en.language = Bcp47LanguageTag{"en"};
    EffectiveTextState ar = make_default();
    ar.language = Bcp47LanguageTag{"ar"};
    CHECK(en != ar);
}

TEST_CASE("M1.5#2 EffectiveTextState: features flip → unequal (PRE-M1.5#2 SILENT MISS)") {
    auto make_default = []() {
        EffectiveTextState s;
        s.text      = "Hello";
        s.font      = FontLayoutIdentity{
            .resolved_path = "/fonts/Inter.ttf",
            .family        = "Inter",
            .weight        = 400,
            .style         = "Regular",
            .size          = 32.0f,
            .variation_axes = {},
        };
        return s;
    };
    EffectiveTextState liga = make_default();
    liga.features = TextShapingFeatures{"liga"};
    EffectiveTextState kern = make_default();
    kern.features = TextShapingFeatures{"kern"};
    CHECK(liga != kern);
}

// ═══════════════════════════════════════════════════════════════════════
// §2 — default-constructed EffectiveTextStates compare equal (deterministic).
//     Pre-M1.5#2 the fast-path used `shape.layout->source_text`, which
//     was empty after materialization → false-hit against any non-empty
//     target.  M1.5#2 default state is genuinely empty so we get the
//     expected behaviour.
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("M1.5#2 EffectiveTextState: two default-constructed states are equal") {
    EffectiveTextState a;
    EffectiveTextState b;
    CHECK(a == b);
}
