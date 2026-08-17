// ─── test_visual_preset_registry.cpp — VISUAL-SSOT-01 dedicated test ──────
//
// Verifies the SINGLE-REGISTRY invariant from ADR-029:
//   1. The 7 canonical visual presets are registered with shape
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
TEST_CASE("VisualPresetRegistry: canonical 7-preset vocabulary is registered (A1-A3)") {

    SUBCASE("A1) the 7 canonical presets resolve exactly") {
        const auto& r = builtin_visual_preset_registry();
        const std::vector<std::string> want = {
            "active_word_pop", "caption_card", "image_focus_in",
            "location_card", "lower_third_safe", "organization_card",
            "subtitle_card",
        };
        REQUIRE(r.available() == want);
    }

    SUBCASE("A2) every descriptor has non-empty id + >=1 capability") {
        const auto& r = builtin_visual_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK_FALSE(d.id.empty());
            CHECK(d.version >= 1);
            CHECK_FALSE(d.capabilities.empty());
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

// ─────────────────────────────────────────────────────────────────────────
// TIER B — lower_third_safe spot-check (the richest descriptor)
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("VisualPresetRegistry: lower_third_safe carries the full paint recipe (B1)") {
    const auto& r = builtin_visual_preset_registry();
    const auto& d = r.get("lower_third_safe");

    CHECK(d.supported_layer == chronon3d::registry::VisualLayerKind::Text);
    CHECK(d.style.font_family == "Poppins");
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
    CHECK(d.animation.preset == "fade_in");
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
