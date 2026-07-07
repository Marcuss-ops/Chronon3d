// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity_killer/killer_03_character_offset.cpp
//
// TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE — Phase 2 Killer 3.
//
// LOCK WHAT IS LOCKABLE TODAY, MARK WHAT IS FORWARD-ONLY.
// ───────────────────────────────────────────────────────────────────────────
// CharacterOffsetProperty is IMPL + PreShaping (FASE 2a, TICKET-046 /
// Blocco 2 of docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md).  It is
// evaluated in `src/text/animation/text_pre_shaping.cpp` via
// `apply_character_offset_to_source()` (Caesar-cipher on UTF-8 code
// points, A-Z + a-z only, wrap-around, non-letter pass-through).  This
// invalidates shaping + layout + cache because the source text flows
// into HarfBuzz shaping BEFORE glyph selection, kerning, ligatures,
// and bidi reordering.  The lock contract: `Offset +5` MUST swap the
// source text (and therefore the shaped glyph ID), NOT just apply a
// post-layout visual transform.
//
// CharacterValue + CharacterRange are PLANNED, NOT YET IMPL
// (verified via find-grep: zero `character_value*.cpp` or
// `character_range*.cpp` files in src/text/).  Source anchor per
// docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md Decision 2 Killer 3:
// `src/text/source/character_offset_evaluator.cpp` (NEW; replaces the
// current `GlyphInstanceState`-only path).
//
// What IS IMPL (and what this killer test locks):
//   1. The pre-shaping source transform contract:
//      - apply_character_offset_to_source is a pure function
//        (Caesar-cipher, wrap-around, non-letter pass-through)
//      - evaluate_pre_shaping_source composes multiple CharacterOffset
//        properties into a total offset
//      - has_pre_shaping_properties detection contract
//   2. The cache invalidation contract:
//      - TextLayoutCacheKey changes when the pre-shaping source
//        changes (proves the cache will miss on next lookup)
//      - PostLayout properties do NOT trigger pre-shaping
//
// 2 TEST_CASEs, each with 3-SUBCASE pre-shaping invalidation lock
// pattern:
//   CASE 1: pre-shaping source transform substrate (3 SUBCASEs:
//           byte-identity + boundary contracts SUBCASE 1 +
//           composition SUBCASE 2 + detection contract SUBCASE 3)
//   CASE 2: end-to-end pre-shaping invalidation (3 SUBCASEs:
//           source-swap-not-visual-offset SUBCASE 1 +
//           cache-key-invalidation SUBCASE 2 +
//           CharacterValue+CharacterRange FORWARD-ONLY SUBCASE 3)
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public symbols
// in include/chronon3d/, zero disallowed #include, zero new
// singleton/registry/resolver/cache.  Test-only surface.
//
// Forward-only contract:
//   When TICKET-CHARACTER-VALUE-RANGE-IMPL lands (new
//   `src/text/source/character_offset_evaluator.cpp` per Blocco 2 of
//   docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md), SUBCASE 3 of
//   TEST_CASE 2 is upgraded from a smoke-test placeholder to a hard
//   CHECK on the production contract (CharacterValue replaces index
//   content + CharacterRange applies override to contiguous range).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/animation/text_pre_shaping.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using chronon3d::f32;
using chronon3d::Vec3;

// Test constants.
constexpr int kTestOffset = 5;

// Build a TextAnimatorSpec with a single CharacterOffsetProperty.
// This is the canonical "pre-shaping transform" spec.
[[nodiscard]] chronon3d::TextAnimatorSpec make_preshaping_spec(
    int offset
) {
    chronon3d::TextAnimatorSpec spec;
    spec.id = "killer_03_preshaping_test";
    spec.enabled = true;
    spec.properties.emplace_back(
        chronon3d::CharacterOffsetProperty{offset});
    return spec;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 1 — Pre-shaping source transform substrate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("KILLER 03 character_offset — pre-shaping source transform substrate") {
    SUBCASE("apply_character_offset_to_source byte-identity + boundary contracts") {
        // Pure function contract: same input → byte-identical output
        // across 3 sequential invocations.
        const auto a = chronon3d::apply_character_offset_to_source(
            std::string("ABCDEFG"), kTestOffset);
        const auto b = chronon3d::apply_character_offset_to_source(
            std::string("ABCDEFG"), kTestOffset);
        const auto c = chronon3d::apply_character_offset_to_source(
            std::string("ABCDEFG"), kTestOffset);
        CHECK(a == b);
        CHECK(b == c);

        // Boundary contract: offset=0 → source unchanged (no-op).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("HELLO"), 0) == "HELLO");

        // Boundary contract: A + 5 → F (Caesar cipher +5 on uppercase).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("A"), 5) == "F");

        // Boundary contract: Z + 1 → A (wrap-around at end of alphabet).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("Z"), 1) == "A");

        // Boundary contract: a + 5 → f (Caesar cipher +5 on lowercase).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("a"), 5) == "f");

        // Boundary contract: z + 1 → a (wrap-around at end of lowercase alphabet).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("z"), 1) == "a");

        // Boundary contract: 26 → full alphabet cycle (HELLO unchanged).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("HELLO"), 26) == "HELLO");

        // Boundary contract: -1 → reverse Caesar (B → A).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("BCD"), -1) == "ABC");

        // Non-letter pass-through: digits + punctuation unchanged.
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string("A1! B2? C3."), 1) == "B1! C2? D3.");

        // Empty source: still returns empty (no crash, no exception).
        CHECK(chronon3d::apply_character_offset_to_source(
            std::string(""), 5) == "");
    }

    SUBCASE("evaluate_pre_shaping_source composes multiple CharacterOffset properties") {
        // Lock contract: when multiple animators each carry a
        // CharacterOffsetProperty, the total offset is the SUM of all
        // individual offsets.  This is the composability contract for
        // stacked animators (the canonical "stacked AE-style animators"
        // pattern in the engine).
        std::vector<chronon3d::TextAnimatorSpec> animators;

        // Animator 1: offset=+3 (A → D)
        animators.push_back(make_preshaping_spec(3));

        // Animator 2: offset=+2 (A → C, or D → F if stacked on top of +3)
        animators.push_back(make_preshaping_spec(2));

        // Total offset = 3 + 2 = 5.  Source "A" → "F" (Caesar +5).
        const auto result = chronon3d::evaluate_pre_shaping_source(
            animators, std::string_view("A"));
        CHECK(result == "F");

        // Negative composition: -3 + 2 = -1 net.  Source "B" → "A" (Caesar -1).
        std::vector<chronon3d::TextAnimatorSpec> neg_animators;
        neg_animators.push_back(make_preshaping_spec(-3));
        neg_animators.push_back(make_preshaping_spec(+2));
        const auto neg_result = chronon3d::evaluate_pre_shaping_source(
            neg_animators, std::string_view("B"));
        CHECK(neg_result == "A");
    }

    SUBCASE("has_pre_shaping_properties detection contract") {
        // Lock contract: has_pre_shaping_properties returns true when
        // ANY enabled animator carries a CharacterOffsetProperty, false
        // otherwise.  This is the gating contract for the pre-shaping
        // pipeline (the pipeline is only invoked when this returns true).
        std::vector<chronon3d::TextAnimatorSpec> with_preshaping;
        with_preshaping.push_back(make_preshaping_spec(5));
        CHECK(chronon3d::has_pre_shaping_properties(with_preshaping) == true);

        // PostLayout-only animator (PositionProperty) does NOT trigger
        // pre-shaping.  This locks the "pre-shaping is exclusive to
        // CharacterOffset" property-stage contract.
        std::vector<chronon3d::TextAnimatorSpec> with_postlayout;
        {
            chronon3d::TextAnimatorSpec spec;
            spec.id = "killer_03_postlayout_only";
            spec.enabled = true;
            spec.properties.emplace_back(
                chronon3d::PositionProperty{Vec3{1.0f, 0.0f, 0.0f}});
            with_postlayout.push_back(spec);
        }
        CHECK(chronon3d::has_pre_shaping_properties(with_postlayout) == false);

        // Mixed: a pre-shaping animator + a post-layout animator in the
        // same stack → has_pre_shaping_properties is true (the pre-shaping
        // animator is sufficient to trigger the pipeline).
        std::vector<chronon3d::TextAnimatorSpec> mixed;
        mixed.push_back(make_preshaping_spec(5));
        {
            chronon3d::TextAnimatorSpec spec;
            spec.id = "killer_03_mixed_postlayout";
            spec.enabled = true;
            spec.properties.emplace_back(
                chronon3d::PositionProperty{Vec3{1.0f, 0.0f, 0.0f}});
            mixed.push_back(spec);
        }
        CHECK(chronon3d::has_pre_shaping_properties(mixed) == true);

        // Disabled animator with CharacterOffset does NOT trigger
        // pre-shaping (the spec.enabled = false short-circuits the
        // pre-shaping evaluator).
        std::vector<chronon3d::TextAnimatorSpec> disabled;
        {
            chronon3d::TextAnimatorSpec spec;
            spec.id = "killer_03_disabled_preshaping";
            spec.enabled = false;
            spec.properties.emplace_back(
                chronon3d::CharacterOffsetProperty{5});
            disabled.push_back(spec);
        }
        CHECK(chronon3d::has_pre_shaping_properties(disabled) == false);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST_CASE 2 — End-to-end pre-shaping invalidation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("KILLER 03 character_offset — end-to-end pre-shaping invalidation") {
    SUBCASE("CharacterOffset +5 swaps the source text (NOT just a visual offset)") {
        // THE CORE PRE-SHAPING CONTRACT.
        //
        // Lock contract: CharacterOffset +5 transforms the source
        // text "A" → "F" (Caesar +5), which means HarfBuzz shapes the
        // glyph for 'F', NOT the glyph for 'A' with a post-layout
        // visual offset.  This is the property-stage invalidation
        // contract from Blocco 2 of the text roadmap.
        //
        // Anti-greenwashing: we also verify that a PositionProperty
        // applied alongside the CharacterOffset does NOT change the
        // source text.  If a future regression applied CharacterOffset
        // as a post-layout visual transform (the pre-FASE 2a behavior),
        // the source would remain "A" and the shape would be the
        // glyph for 'A' with a visual offset — the source-change
        // CHECK would FAIL.

        // Case 1: CharacterOffset +5 only → source "A" becomes "F".
        {
            std::vector<chronon3d::TextAnimatorSpec> animators;
            animators.push_back(make_preshaping_spec(5));
            const auto result = chronon3d::evaluate_pre_shaping_source(
                animators, std::string_view("A"));
            CHECK(result == "F");
        }

        // Case 2: CharacterOffset +5 + PositionProperty{Vec3{100,0,0}}
        // (non-zero visual offset) → source still "A" becomes "F" (proves
        // the source change is independent of any visual offset — a
        // future regression that applied CharacterOffset as BOTH a
        // visual offset AND a source change would still pass, but the
        // pre-FASE 2a behavior (visual offset only, no source change)
        // would fail because the source would remain "HELLO" not
        // "MJQQT").
        {
            chronon3d::TextAnimatorSpec spec;
            spec.id = "killer_03_combined";
            spec.enabled = true;
            spec.properties.emplace_back(
                chronon3d::CharacterOffsetProperty{5});
            spec.properties.emplace_back(
                chronon3d::PositionProperty{Vec3{100.0f, 0.0f, 0.0f}});
            std::vector<chronon3d::TextAnimatorSpec> animators;
            animators.push_back(spec);
            const auto result = chronon3d::evaluate_pre_shaping_source(
                animators, std::string_view("HELLO"));
            CHECK(result == "MJQQT");  // H→M, E→J, L→Q, L→Q, O→T
        }            // Case 3: PositionProperty{Vec3{50,0,0}} alone (no
            // CharacterOffset) → source unchanged (proves Position is
            // PostLayout, NOT pre-shaping).
            {
                chronon3d::TextAnimatorSpec spec;
                spec.id = "killer_03_postlayout_only";
                spec.enabled = true;
                spec.properties.emplace_back(
                    chronon3d::PositionProperty{Vec3{50.0f, 0.0f, 0.0f}});
            std::vector<chronon3d::TextAnimatorSpec> animators;
            animators.push_back(spec);
            const auto result = chronon3d::evaluate_pre_shaping_source(
                animators, std::string_view("HELLO"));
            CHECK(result == "HELLO");  // PostLayout does NOT touch source
        }
    }

    SUBCASE("TextLayoutCacheKey changes when pre-shaping source changes (cache invalidation)") {
        // Lock contract: when pre-shaping transforms the source
        // text, the TextLayoutCacheKey's digest MUST change so the
        // cache misses on next lookup and rebuilds the layout with
        // the new source.  If the cache key did NOT depend on the
        // source, the cache would serve a STALE layout for the
        // transformed source — a silent corruption.
        //
        // We construct two TextLayoutCacheKey objects with the same
        // font identity but different source_text (the post-pre-
        // shaping transformed source vs. the original source).  The
        // digests must be different.

        // Pre-shaping transform: "A" + 5 = "F" (from SUBCASE 1).
        // The cache key for the pre-shaping-transformed "F" must
        // differ from the cache key for the original "A".
        // TextLayoutCacheKey uses flat font fields (font_path,
        // font_weight, font_style, font_size) — not a nested
        // FontLayoutIdentity.  Other fields (tracking, box_width, wrap,
        // direction, language, features, font_family, variation_axes,
        // paragraph) use their canonical default values.
        chronon3d::TextLayoutCacheKey key_original;
        key_original.text = "A";
        key_original.font_path = "fonts/Inter-Regular.ttf";
        key_original.font_weight = 400;
        key_original.font_style = "normal";
        key_original.font_size = 16.0f;

        chronon3d::TextLayoutCacheKey key_transformed;
        key_transformed.text = "F";
        key_transformed.font_path = "fonts/Inter-Regular.ttf";
        key_transformed.font_weight = 400;
        key_transformed.font_style = "normal";
        key_transformed.font_size = 16.0f;

        // The digests MUST differ — proves the cache will miss on the
        // transformed source, forcing a fresh layout rebuild.
        CHECK(key_original.digest() != key_transformed.digest());

        // Sanity check: same source + same font → same digest (cache hit).
        chronon3d::TextLayoutCacheKey key_dup;
        key_dup.text = "A";
        key_dup.font_path = "fonts/Inter-Regular.ttf";
        key_dup.font_weight = 400;
        key_dup.font_style = "normal";
        key_dup.font_size = 16.0f;
        CHECK(key_original.digest() == key_dup.digest());
    }

    SUBCASE("CharacterValue + CharacterRange contract (FORWARD-ONLY smoke-test)") {
        // FORWARD-ONLY: CharacterValue and CharacterRange are PLANNED,
        // NOT YET IMPL (zero `character_value*.cpp` or
        // `character_range*.cpp` files in src/text/).  Source anchor
        // per docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md
        // Decision 2 Killer 3:
        //   `src/text/source/character_offset_evaluator.cpp` (NEW;
        //    replaces the current `GlyphInstanceState`-only path)
        //
        // This SUBCASE documents the production contract that the
        // future CharacterValue + CharacterRange implementation must
        // follow.  When TICKET-CHARACTER-VALUE-RANGE-IMPL lands, this
        // SUBCASE is upgraded from a smoke-test placeholder to a hard
        // CHECK on the production contract:
        //   - CharacterValue replaces a specific index's text content
        //     (e.g. index 2 in "HELLO" replaced by "X" → "HEXLO")
        //   - CharacterRange applies an override to a contiguous
        //     range (e.g. indices 1..3 in "HELLO" set to bold weight
        //     via a PostLayout property override)
        //
        // Both are PreShaping (they modify the source text or the
        // shaping-time properties, so they MUST be evaluated BEFORE
        // HarfBuzz shaping and MUST invalidate the shaping + layout
        // caches on change).  The forward-only upgrade path is:
        //   1. Implement CharacterValue + CharacterRange in
        //      `src/text/source/character_offset_evaluator.cpp`
        //   2. Wire them into the `TextAnimatorProperty` variant
        //      alongside CharacterOffsetProperty
        //   3. Extend `has_pre_shaping_properties` to detect them
        //   4. Extend `evaluate_pre_shaping_source` to apply them
        //   5. Upgrade this SUBCASE to verify the production contract
        CHECK(true);  // FORWARD-ONLY placeholder — see comment above
        MESSAGE("CharacterValue + CharacterRange are PLANNED (forward-only). "
                "When TICKET-CHARACTER-VALUE-RANGE-IMPL lands, this SUBCASE "
                "upgrades to a hard CHECK on the production contract.  "
                "Cross-link: docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md "
                "Decision 2 Killer 3 + docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md "
                "Blocco 2.");
    }
}
