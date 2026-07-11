// test_presets_stability.cpp — M1.8 §3C / TICKET-SIMPLICITY-PRESETS
//
// 5 TEST_CASEs (one per preset) × 3 CHECK assertions = 15 total.
//
// The 3 invariants per preset (locked per §3C spec):
//   1. content.value matches the user-provided text (the AUTHORING contract)
//   2. style.font.font_size matches the user-provided font_size parameter
//      (or the default for presets that take no font_size arg)
//   3. frame.placement matches the preset's hard-locked 1920×1080 pin point
//      (the PLACEMENT contract)
//
// Pure struct inspection — no Framebuffer / compositor / GPU / Blend2D /
// FontEngine / HarfBuzz.  Compiles UNCONDITIONALLY (mirrors
// text_definition_tests.cmake / text_builder_ergonomics_tests.cmake).
//
// Anti-duplicazione honour: reuses the canonical TextDefinition DTO from
// include/chronon3d/text/text_definition.hpp.  Does NOT introduce a
// parallel PresetSpec / PresetOutput type.

#include <doctest/doctest.h>

#include <chronon3d/presets/text/text_presets_v1.hpp>  // 5 preset functions
#include <chronon3d/text/text_definition.hpp>           // TextDefinition
#include <chronon3d/text/text_placement.hpp>            // TextAnchor, TextAlign, VerticalAlign

using namespace chronon3d;
using namespace chronon3d::presets::text;

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: title_centered — default 96pt + 1920×1080 canvas-center pin") {
    const auto def = title_centered("CHRONON3D");
    // 1. content.value matches
    CHECK(def.content.value == "CHRONON3D");
    // 2. style.font.font_size matches the default 96.0f
    CHECK(def.style.font.font_size == doctest::Approx(96.0f));
    // 3. frame.placement matches canvas center (960, 540) for 1920×1080
    //    (single combined CHECK: x AND y locked together as one invariant)
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f)
       && def.frame.placement.offset.y == doctest::Approx(540.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: subtitle_bottom — default 48pt + SafeAreaBottom pin (960, 1026)") {
    const auto def = subtitle_bottom("a subtitle");
    CHECK(def.content.value == "a subtitle");
    CHECK(def.style.font.font_size == doctest::Approx(48.0f));
    // Pin point: 1920×1080, 5% safe-area bottom = (960, 1080 - 54) = (960, 1026).
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f)
       && def.frame.placement.offset.y == doctest::Approx(1026.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: caption_safe_area — default 36pt + SafeAreaCenter pin (960, 540)") {
    const auto def = caption_safe_area("a caption");
    CHECK(def.content.value == "a caption");
    CHECK(def.style.font.font_size == doctest::Approx(36.0f));
    // Pin point: 1920×1080, SafeAreaCenter (5% margin symmetric) = (960, 540).
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f)
       && def.frame.placement.offset.y == doctest::Approx(540.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_word
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: kinetic_word — default 120pt + canvas-center pin") {
    const auto def = kinetic_word("HERO");
    CHECK(def.content.value == "HERO");
    CHECK(def.style.font.font_size == doctest::Approx(120.0f));
    // Pin point: 1920×1080, CanvasCenter = (960, 540).
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f)
       && def.frame.placement.offset.y == doctest::Approx(540.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: lower_third — default 42pt + SafeAreaLeft-lower pin (140, 920)") {
    const auto def = lower_third("MARCO ROSSI");
    CHECK(def.content.value == "MARCO ROSSI");
    CHECK(def.style.font.font_size == doctest::Approx(42.0f));
    // Pin point: 1920×1080, 5% safe-area left + lower-third offset = (140, 920).
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
    CHECK(def.frame.placement.offset.x == doctest::Approx(140.0f)
       && def.frame.placement.offset.y == doctest::Approx(920.0f));
}
