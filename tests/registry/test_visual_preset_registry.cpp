// ─── test_visual_preset_registry.cpp — VISUAL-SSOT-01 dedicated test ──────
//
// Verifies the SINGLE-REGISTRY invariant from ADR-029:
//   1. The canonical visual presets are registered with shape
//      { id, version, supported_layer, style, anchor, animation,
//        fallback_anchors, capabilities }.
//   2. Every descriptor has a non-empty id, version >= 1, a non-empty
//      anchor type and at least one capability tag.
//   3. The semantic→preset vocabulary is locked: the presets PipelineGen's
//      SemanticOverlayResolver may emit all resolve through this registry
//      (caption_card / active_word_pop / subtitle_card / lower_third_safe /
//      organization_card / location_card / image_focus_in).
//   4. `builtin_visual_preset_registry()` is frozen: unknown ids throw on
//      `get`, and later registration is rejected.
//
// Mirrors tests/registry/test_text_preset_descriptor.cpp: pure registry
// inspection, no LayerBuilder / SceneBuilder / renderer instantiation.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/visual_preset_registry.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using chronon3d::registry::VisualPresetDescriptor;
using chronon3d::registry::builtin_visual_preset_registry;
using chronon3d::registry::make_default_visual_preset_registry;

// ─────────────────────────────────────────────────────────────────────────
// TIER A — canonical vocabulary + descriptor completeness
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: canonical 21-preset vocabulary is registered (A1-A3)") {

    SUBCASE("A1) the canonical 21 presets resolve exactly") {
        const auto& r = builtin_visual_preset_registry();
        const std::vector<std::string> want = {
            "active_word_pop", "caption_card", "image_fade_in",
            "image_focus_in", "image_scale_in", "image_slide_left",
            "image_slide_right", "location_card", "lower_third_safe",
            "name_fade_in", "name_pop_in", "name_scale_in",
            "name_slide_left", "name_slide_up", "organization_card",
            "phrase_fade_in", "phrase_scale_in", "phrase_slide_up",
            "phrase_soft_pop", "phrase_word_reveal", "subtitle_card",
        };
        const auto available = r.available();
        for (const auto& w : want) {
            CHECK(std::find(available.begin(), available.end(), w) != available.end());
        }
    }

    SUBCASE("A2) every descriptor has non-empty id + >=1 capability") {
        const auto& r = builtin_visual_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK_FALSE(d.id.empty());
            CHECK(d.version >= 1);
            CHECK_FALSE(d.capabilities.empty());
            CHECK(std::find(d.capabilities.begin(), d.capabilities.end(), "2d") != d.capabilities.end());
            CHECK_FALSE(d.semantic_role.empty());
        }
    }

    SUBCASE("A3) entity cards carry the local-background + card capabilities") {
        const auto& r = builtin_visual_preset_registry();
        for (const auto& id : {"caption_card", "subtitle_card", "lower_third_safe",
                               "organization_card", "location_card"}) {
            CAPTURE(id);
            const auto& d = r.get(id);
            const std::set<std::string> caps(d.capabilities.begin(), d.capabilities.end());
            CHECK(caps.count("card") == 1);
            CHECK(caps.count("local_background") == 1);
        }
    }
}

TEST_CASE("VisualPresetRegistry: 2D category presets are complete") {
    const auto& r = builtin_visual_preset_registry();
    const std::vector<std::string> image_ids = {
        "image_fade_in", "image_focus_in", "image_scale_in",
        "image_slide_left", "image_slide_right"};
    const std::vector<std::string> name_ids = {
        "name_fade_in", "name_pop_in", "name_scale_in",
        "name_slide_left", "name_slide_up"};
    const std::vector<std::string> phrase_ids = {
        "phrase_fade_in", "phrase_scale_in", "phrase_slide_up",
        "phrase_soft_pop", "phrase_word_reveal"};

    for (const auto& id : image_ids) {
        const auto& d = r.get(id);
        CHECK(d.semantic_role == "image");
        CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Image);
        CHECK(std::find(d.capabilities.begin(), d.capabilities.end(), "2d") != d.capabilities.end());
    }
    for (const auto& id : name_ids) {
        const auto& d = r.get(id);
        CHECK(d.semantic_role == "name");
        CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Text);
        CHECK(d.style.font_asset == "assets/fonts/Poppins-Bold.ttf");
        CHECK(d.anchor.type == "lower_third");
    }
    for (const auto& id : phrase_ids) {
        const auto& d = r.get(id);
        CHECK(d.semantic_role == "important_phrase");
        CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Text);
        CHECK(d.anchor.type == "safe_area");
        CHECK(d.animation.exit_duration_frames.value() == 6);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TIER B — lower_third_safe spot-check (the richest descriptor)
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: lower_third_safe carries the full paint recipe (B1)") {
    const auto& r = builtin_visual_preset_registry();
    const auto& d = r.get("lower_third_safe");

    CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Text);
    CHECK(d.style.font_family == "Poppins");
    CHECK(d.style.font_asset == "assets/fonts/Poppins-Bold.ttf");
    REQUIRE(d.style.font_weight.has_value());
    CHECK(d.style.font_weight.value() == 700);
    REQUIRE(d.style.font_size.has_value());
    CHECK(d.style.font_size.value() == doctest::Approx(58.0f));
    CHECK(d.style.fill == "#FFFFFF");
    CHECK(d.style.stroke_color == "#000000");
    CHECK(d.style.shadow_color == "#000000");
    REQUIRE(d.style.shadow_blur.has_value());
    CHECK(d.style.shadow_blur.value() == doctest::Approx(16.0f));
    CHECK(d.anchor.type == "lower_third");
    CHECK(d.anchor.alignment == "left");
    // Registered Chronon layer motion (see motion_preset_packs.hpp); the
    // per-glyph word treatment stays owned by the text preset pipeline.
    CHECK(d.animation.preset == "focus_in");
    CHECK(d.fallback_anchors == std::vector<std::string>{"lower_right", "top_left", "top_right"});
}

// ─────────────────────────────────────────────────────────────────────────
// TIER C — image preset is anchored to the image layer kind
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: image_focus_in targets the image layer (C1)") {
    const auto& r = builtin_visual_preset_registry();
    const auto& d = r.get("image_focus_in");
    CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Image);
    CHECK(d.anchor.type == "image_right");
    // Image presets own their canonical box + fit (ADR-029: the former
    // PipelineGen IMAGE_OVERLAY transport shape lives here, not in Go).
    REQUIRE(d.box_width.has_value());
    CHECK(d.box_width.value() == doctest::Approx(260.0f));
    REQUIRE(d.box_height.has_value());
    CHECK(d.box_height.value() == doctest::Approx(260.0f));
    CHECK(d.fit == "contain");
}

TEST_CASE("VisualPresetRegistry: style profiles resolve in Chronon only") {
    const auto& r = builtin_visual_preset_registry();

    const auto discovery = r.get_for_profile("lower_third_safe", "discovery");
    const auto young = r.get_for_profile("lower_third_safe", "young");
    const auto crime = r.get_for_profile("lower_third_safe", "crime");

    CHECK(discovery.style.font_asset == "assets/fonts/Poppins-Bold.ttf");
    CHECK(young.style.font_family == "Poppins");
    CHECK(young.style.stroke_color == "#22D3EE");
    CHECK(crime.style.font_family == "Poppins");
    CHECK(crime.style.font_asset == "assets/fonts/Poppins-Bold.ttf");
    CHECK(crime.style.stroke_color == "#EF4444");
    CHECK(crime.animation.preset == "soft_pop");

    CHECK_THROWS_AS(
        (void)r.get_for_profile("lower_third_safe", "unknown"),
        std::runtime_error);
}

// ─────────────────────────────────────────────────────────────────────────
// TIER E — base_preset materializer mapping is owned by the registry
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: base_preset materializer mapping is canonical (E1-E2)") {
    const auto& r = builtin_visual_preset_registry();

    SUBCASE("E1) text presets lower onto their canonical text materializer") {
        CHECK(r.get("caption_card").base_preset == "caption_safe_area");
        CHECK(r.get("active_word_pop").base_preset == "kinetic_word");
        CHECK(r.get("subtitle_card").base_preset == "subtitle_bottom");
        CHECK(r.get("lower_third_safe").base_preset == "lower_third");
        CHECK(r.get("organization_card").base_preset == "lower_third");
        CHECK(r.get("location_card").base_preset == "lower_third");
        for (const auto& id : {"name_fade_in", "name_pop_in", "name_scale_in",
                               "name_slide_left", "name_slide_up"}) {
            CAPTURE(id);
            CHECK(r.get(id).base_preset == "lower_third");
        }
        for (const auto& id : {"phrase_fade_in", "phrase_scale_in", "phrase_slide_up",
                               "phrase_soft_pop", "phrase_word_reveal"}) {
            CAPTURE(id);
            CHECK(r.get(id).base_preset == "caption_safe_area");
        }
    }

    SUBCASE("E2) image presets carry no text materializer") {
        for (const auto& id : {"image_fade_in", "image_focus_in", "image_scale_in",
                               "image_slide_left", "image_slide_right"}) {
            CAPTURE(id);
            CHECK(r.get(id).base_preset.empty());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TIER D — fail-safe paths
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: fail-safe paths (D1-D2)") {
    SUBCASE("D1) get() throws on unknown id; contains() returns false") {
        const auto& r = builtin_visual_preset_registry();
        CHECK_FALSE(r.contains("phantom_unknown_preset"));
        CHECK_THROWS_AS((void)r.get("phantom_unknown_preset"), std::runtime_error);
    }

    SUBCASE("D2) builtin registry is frozen and rejects later registration") {
        auto r = builtin_visual_preset_registry();  // by-value copy (frozen flag preserved).
        CHECK(r.is_frozen());
        VisualPresetDescriptor rogue;
        rogue.id = "rogue_addition";
        CHECK_THROWS_AS(r.register_preset(std::move(rogue)), std::runtime_error);
    }
}
