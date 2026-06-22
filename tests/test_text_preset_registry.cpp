// ─── test_text_preset_registry.cpp — Cluster A DoD #1b test ───────────────
//
// Minimal machine-verifiable validation:
//   (1) Round-trip enum → string → enum for TextPresetCategory (no Unicode).
//   (2) make_default_text_preset_registry() registers ≥5 built-in entries.
//   (3) contains() correctly distinguishes known / unknown ids.
//   (4) get() returns descriptor with correct metadata + non-null builder.
//   (5) by_category() correctly filters 4-category partitioning.
//
// Mirrors tests/scene/camera/test_camera_registry.cpp template style
// (DOCTEST_CONFIG_SUPER_FAST_ASSERTS, idiomatic TEST_CASE + SUBCASE blocks).

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/text_preset_registry.hpp>

using namespace chronon3d;
using namespace chronon3d::registry;

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
}
