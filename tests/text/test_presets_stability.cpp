// test_presets_stability.cpp — M1.8 §3C / TICKET-SIMPLICITY-PRESETS
//
// 5 TEST_CASEs (one per preset) × multiple canvas aspect ratios.
//
// Invariants per preset:
//   1. content.value matches the user-provided text (the AUTHORING contract)
//   2. style.font.font_size matches the user-provided font_size parameter
//      (or the default for presets that take no font_size arg)
//   3. frame.placement uses semantic placement kinds (no hard-coded coordinates)
//   4. frame.size is derived from the supplied CanvasInfo
//   5. presets adapt to 16:9, 9:16, 1:1 and custom resolutions
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
#include <chronon3d/text/text_placement.hpp>            // TextAnchor, TextAlign, VerticalAlign, SafeAreaPreset
#include <chronon3d/text/resolve_text_placement.hpp>    // CanvasInfo

using namespace chronon3d;
using namespace chronon3d::presets::text;

namespace {

CanvasInfo make_canvas_16_9() {
    return CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset::Landscape16x9);
}

CanvasInfo make_canvas_9_16() {
    return CanvasInfo::with_safe_area(1080.0f, 1920.0f, SafeAreaPreset::Portrait9x16);
}

CanvasInfo make_canvas_1_1() {
    return CanvasInfo::with_safe_area(1080.0f, 1080.0f, SafeAreaPreset::Square1x1);
}

CanvasInfo make_canvas_4k() {
    return CanvasInfo::with_safe_area(3840.0f, 2160.0f, SafeAreaPreset::Landscape16x9);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: title_centered — semantic placement + responsive box") {
    const auto def = title_centered("CHRONON3D", make_canvas_16_9());
    CHECK(def.content.value == "CHRONON3D");
    CHECK(def.style.font.font_size == doctest::Approx(96.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.align == TextAlign::Center);
    CHECK(def.frame.size.x == doctest::Approx(900.0f));
    CHECK(def.frame.size.y == doctest::Approx(160.0f));
}

TEST_CASE("Presets: title_centered adapts to 9:16") {
    const auto def = title_centered("CHRONON3D", make_canvas_9_16());
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.size.x == doctest::Approx(506.25f));
    CHECK(def.frame.size.y == doctest::Approx(284.44f));
}

TEST_CASE("Presets: title_centered adapts to 1:1") {
    const auto def = title_centered("CHRONON3D", make_canvas_1_1());
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.size.x == doctest::Approx(506.25f));
    CHECK(def.frame.size.y == doctest::Approx(160.0f));
}

TEST_CASE("Presets: title_centered adapts to 4K") {
    const auto def = title_centered("CHRONON3D", make_canvas_4k());
    CHECK(def.frame.size.x == doctest::Approx(1800.0f));
    CHECK(def.frame.size.y == doctest::Approx(320.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: subtitle_bottom — semantic placement + responsive box") {
    const auto def = subtitle_bottom("a subtitle", make_canvas_16_9());
    CHECK(def.content.value == "a subtitle");
    CHECK(def.style.font.font_size == doctest::Approx(48.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaBottom);
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.size.x == doctest::Approx(1200.0f));
    CHECK(def.frame.size.y == doctest::Approx(100.0f));
}

TEST_CASE("Presets: subtitle_bottom adapts to 9:16") {
    const auto def = subtitle_bottom("a subtitle", make_canvas_9_16());
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaBottom);
    CHECK(def.frame.size.x == doctest::Approx(675.0f));
    CHECK(def.frame.size.y == doctest::Approx(177.78f));
}

TEST_CASE("Presets: subtitle_bottom adapts to 1:1") {
    const auto def = subtitle_bottom("a subtitle", make_canvas_1_1());
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaBottom);
    CHECK(def.frame.size.x == doctest::Approx(675.0f));
    CHECK(def.frame.size.y == doctest::Approx(100.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: caption_safe_area — semantic placement + responsive box") {
    const auto def = caption_safe_area("a caption", make_canvas_16_9());
    CHECK(def.content.value == "a caption");
    CHECK(def.style.font.font_size == doctest::Approx(36.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaCenter);
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.size.x == doctest::Approx(1640.0f));
    CHECK(def.frame.size.y == doctest::Approx(80.0f));
}

TEST_CASE("Presets: caption_safe_area adapts to 9:16") {
    const auto def = caption_safe_area("a caption", make_canvas_9_16());
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaCenter);
    CHECK(def.frame.size.x == doctest::Approx(922.5f));
    CHECK(def.frame.size.y == doctest::Approx(142.22f));
}

TEST_CASE("Presets: caption_safe_area adapts to 1:1") {
    const auto def = caption_safe_area("a caption", make_canvas_1_1());
    CHECK(def.frame.placement.kind == TextPlacementKind::SafeAreaCenter);
    CHECK(def.frame.size.x == doctest::Approx(922.5f));
    CHECK(def.frame.size.y == doctest::Approx(80.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_word
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: kinetic_word — semantic placement + responsive box") {
    const auto def = kinetic_word("HERO", make_canvas_16_9());
    CHECK(def.content.value == "HERO");
    CHECK(def.style.font.font_size == doctest::Approx(120.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.size.x == doctest::Approx(1600.0f));
    CHECK(def.frame.size.y == doctest::Approx(240.0f));
}

TEST_CASE("Presets: kinetic_word adapts to 9:16") {
    const auto def = kinetic_word("HERO", make_canvas_9_16());
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.size.x == doctest::Approx(900.0f));
    CHECK(def.frame.size.y == doctest::Approx(426.67f));
}

TEST_CASE("Presets: kinetic_word adapts to 1:1") {
    const auto def = kinetic_word("HERO", make_canvas_1_1());
    CHECK(def.frame.placement.kind == TextPlacementKind::CanvasCenter);
    CHECK(def.frame.size.x == doctest::Approx(900.0f));
    CHECK(def.frame.size.y == doctest::Approx(240.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Presets: lower_third — semantic placement + responsive box") {
    const auto def = lower_third("MARCO ROSSI", make_canvas_16_9());
    CHECK(def.content.value == "MARCO ROSSI");
    CHECK(def.style.font.font_size == doctest::Approx(42.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::BottomLeft);
    CHECK(def.frame.anchor == TextAnchor::TopLeft);
    CHECK(def.frame.align == TextAlign::Left);
    CHECK(def.frame.size.x == doctest::Approx(1640.0f));
    CHECK(def.frame.size.y == doctest::Approx(100.0f));
}

TEST_CASE("Presets: lower_third adapts to 9:16") {
    const auto def = lower_third("MARCO ROSSI", make_canvas_9_16());
    CHECK(def.frame.placement.kind == TextPlacementKind::BottomLeft);
    CHECK(def.frame.size.x == doctest::Approx(922.5f));
    CHECK(def.frame.size.y == doctest::Approx(177.78f));
}

TEST_CASE("Presets: lower_third adapts to 1:1") {
    const auto def = lower_third("MARCO ROSSI", make_canvas_1_1());
    CHECK(def.frame.placement.kind == TextPlacementKind::BottomLeft);
    CHECK(def.frame.size.x == doctest::Approx(922.5f));
    CHECK(def.frame.size.y == doctest::Approx(100.0f));
}
