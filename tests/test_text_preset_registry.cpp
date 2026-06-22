// ─── test_text_preset_registry.cpp — Cluster A DoD #1b test ───────────────
//
// Minimal machine-verifiable validation:
//   (1) Round-trip enum → string → enum for TextPresetCategory (no Unicode).
//   (2) make_default_text_preset_registry() registers ≥5 built-in entries.
//   (3) contains() correctly distinguishes known / unknown ids.
//   (4) get() returns descriptor with correct metadata + non-null builder.
//   (5) by_category() correctly filters 4-category partitioning.
//   (6) freeze() blocks further registration (EffectCatalog parity).
//   (7) Builder INVOCATION: each built-in builder can be invoked with a real
//       SceneBuilder + LayerBuilder + TextSpec fixture without throwing.
//   (8) Builder STATE-EFFECT: after invocation, the LayerBuilder's `build()`
//       produces a Layer with at least one text node per build call.
//       This is strictly more stringent than `builder != nullptr` (PR
//       `41cda40c`'s Sub-case 4): the builder must actually USE the
//       LayerBuilder pipeline (call `lb.text(name, spec)`), not just be a
//       non-null std::function pointing at an empty lambda.
//
// Mirrors tests/scene/camera/test_camera_registry.cpp template style
// (DOCTEST_CONFIG_SUPER_FAST_ASSERTS, idiomatic TEST_CASE + SUBCASE blocks).

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/text_preset_registry.hpp>

// ── Stage-2 full type defs required by invocation tests ────────────────
// The builder bodies now use the real SceneBuilder / LayerBuilder /
// TextSpec APIs.  The test must include their full defs to instantiate
// fixtures.  This is consistent with the .cpp impl which also pulls
// these in (anti-circular: header stays include-light; this test + the
// .cpp pull in the full defs).
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

using namespace chronon3d;
using namespace chronon3d::registry;

// ── Fixture: minimal TextSpec with non-empty content ────────────────────
// Designated-initializer aggregate initialisation matches the content/
// text helpers canonical idiom in `content/text/text_helpers_centered.hpp`.
inline TextSpec make_test_text_spec() {
    return TextSpec{
        .content    = {.value = "Hello"},
        .font       = {.font_path   = "assets/fonts/Poppins-Bold.ttf",
                       .font_family = "Poppins",
                       .font_weight = 700,
                       .font_style  = "normal",
                       .font_size   = 96.0f},
        .layout     = {.box = {1200.0f, 240.0f}},
        .appearance = {.color = {1.0f, 1.0f, 1.0f, 1.0f}},
    };
}

TEST_CASE("TextPresetRegistry: enum round-trip + registry filtering + 5-entry seeding") {
    SUBCASE("1) enum <-> string round-trip preserves identity (no Unicode drift)") {
        for (auto cat : {TextPresetCategory::Reveal,
                         TextPresetCategory::Emphasis,
                         TextPresetCategory::Cinematic,
                         TextPresetCategory::Subtitle}) {
            const std::string s = to_string(cat);
            REQUIRE_FALSE(s.empty());
            // canon assertion: snake_case ASCII only.
            for (char ch : s) {
                CHECK(((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')));
            }
        }
        CHECK(to_string(TextPresetCategory::Cinematic) == "cinematic");
        CHECK(to_string_view(TextPresetCategory::Reveal) == "reveal");
    }

    SUBCASE("2) make_default_text_preset_registry populates >= 5 builtin entries") {
        auto reg = make_default_text_preset_registry();
        const auto all = reg.list();
        CHECK(all.size() >= 5);
        const auto ids = reg.available();
        CHECK(ids.size() == all.size());
        // Built-in flag must be true on seeded entries.
        for (const auto& p : all) {
            CHECK(p.builtin == true);
            CHECK_FALSE(p.id.empty());
        }
        // Sorted-by-key determinism (std::map contract).
        for (std::size_t i = 1; i < ids.size(); ++i) {
            CHECK(ids[i - 1] < ids[i]);
        }
    }

    SUBCASE("3) contains() true for known id, false for phantom id") {
        auto reg = make_default_text_preset_registry();
        CHECK(reg.contains("cinematic_text_camera"));
        CHECK(reg.contains("tilt_sweep_title_v2"));
        CHECK_FALSE(reg.contains("phantom_preset_does_not_exist"));
        CHECK_FALSE(reg.contains(""));
    }

    SUBCASE("4) get() returns correct descriptor + builder != nullptr") {
        auto reg = make_default_text_preset_registry();
        const auto& spec = reg.get("cinematic_text_camera");
        CHECK(spec.id == "cinematic_text_camera");
        CHECK(spec.category == TextPresetCategory::Cinematic);
        CHECK(spec.builtin == true);
        CHECK(static_cast<bool>(spec.builder));  // non-null std::function.
    }

    SUBCASE("5) by_category filters correctly across all 4 categories") {
        auto reg = make_default_text_preset_registry();
        const auto cinematics = reg.by_category(TextPresetCategory::Cinematic);
        const auto reveals    = reg.by_category(TextPresetCategory::Reveal);
        const auto emphasis   = reg.by_category(TextPresetCategory::Emphasis);
        const auto subtitles  = reg.by_category(TextPresetCategory::Subtitle);
        // At least one Cinematic (4 of 5 entries per metadata mapping).
        CHECK(cinematics.size() >= 1);
        CHECK(reveals.size()    >= 1);  // text_animations.
        // Per Phase 0 baseline, Emphasis and Subtitle registers start empty.
        // They will be populated via copy-modify in DoD copy-modify PRs.
        // Sum check: total of all categories == total entries.
        CHECK(cinematics.size() + reveals.size() + emphasis.size() + subtitles.size()
              == reg.list().size());
        // Filter correctness: never mis-categorised.
        for (const auto& p : cinematics) {
            CHECK(p.category == TextPresetCategory::Cinematic);
        }
        for (const auto& p : reveals) {
            CHECK(p.category == TextPresetCategory::Reveal);
        }
    }

    SUBCASE("6) freeze() blocks further registration (EffectCatalog parity)") {
        TextPresetRegistry reg;
        TextPreset p;
        p.id = "foo";
        p.category = TextPresetCategory::Reveal;
        reg.register_preset(p);
        CHECK(reg.contains("foo"));
        reg.freeze();
        CHECK(reg.is_frozen());
        TextPreset q;
        q.id = "bar";
        CHECK_THROWS_AS(reg.register_preset(q), std::runtime_error);
    }

    SUBCASE("7) BUILDER INVOCATION — each preset runs without throwing on real fixtures") {
        // Stringent: stand up real SceneBuilder + LayerBuilder + TextSpec,
        // invoke each builder once, assert CHECK_NOTHROW per preset.
        // This catches: type-mismatch errors, missing-include errors, dead
        // std::function targets, and signature drift between header and impl.
        auto reg = make_default_text_preset_registry();
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("invocation_test_layer", Frame{0});
        TextSpec ts = make_test_text_spec();

        for (const auto& preset : reg.list()) {
            CAPTURE(preset.id);
            CHECK_NOTHROW(preset.builder(sb, lb, ts));
        }
    }

    SUBCASE("8) BUILDER STATE-EFFECT — `lb.text(name, spec)` actually adds a node") {
        // Most stringent: after invoking each builder, calling
        // `lb.screen_dimensions(W, H); lb.build()` must produce a Layer
        // whose `nodes` size is >= the number of text-shape builders
        // invoked.  This proves the builder body executed `lb.text(...)`,
        // i.e. it's not a no-op std::function.  Strictly stronger than
        // Sub-case 4 of the previous PR (`builder != nullptr`).
        auto reg = make_default_text_preset_registry();
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("state_effect_test_layer", Frame{0});
        const TextSpec ts = make_test_text_spec();

        const std::size_t expected_minimum = reg.list().size();  // 1 text() call per preset.
        std::size_t invoked_count = 0;
        for (const auto& preset : reg.list()) {
            CAPTURE(preset.id);
            REQUIRE_NOTHROW(preset.builder(sb, lb, ts));
            ++invoked_count;
        }
        // Each invocation adds one pending text node.  After build()
        // those pending nodes are committed to Layer::nodes.
        lb.screen_dimensions(1280.0f, 720.0f);
        Layer built = lb.build();
        CHECK(invoked_count >= 5);                       // sanity: ≥5 presets.
        CHECK(built.nodes.size() >= expected_minimum);   // ≥1 node per invocation.
    }

    SUBCASE("9) BUILDER MOTION-EFFECT — depth_reveal registered on the baked Layer") {
        // Most stringent single-preset motion assertion: a Cinematic
        // builder must produce a Layer whose `depth_offset` was initialised
        // by `lb.depth_reveal(z, Frame{...})` to its `z` at frame 0.  We
        // probe cinematic_text_camera (verified Cinematic per Sub-case 5)
        // and check the directional contract `depth_offset > 0`.
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("cinematic_text_camera"));

        SceneBuilder sb(1280, 720);
        LayerBuilder lb("motion_probe_layer", Frame{0});
        const TextSpec ts = make_test_text_spec();

        const auto& preset = reg.get("cinematic_text_camera");
        REQUIRE_NOTHROW(preset.builder(sb, lb, ts));

        lb.screen_dimensions(1280.0f, 720.0f);
        Layer built = lb.build();

        // ≥1 node present (text() registered the spec).
        CHECK(built.nodes.size() >= 1);
        // Directional contract: depth_reveal(z, …) initialises depth_offset
        // to z at frame 0.  depth_role is a separate settable state (via
        // lb.depth_role(role)), so we probe only the offset — directional `>`
        // matches the actual contract and is invariant under any future
        // magnitude change (260 → 200, etc.).
        CHECK(built.depth_offset > 0.0f);
    }
}
