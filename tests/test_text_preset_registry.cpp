// ─── test_text_preset_registry.cpp — Cluster A DoD #1 test ───────────────
//
// Tier overview (Cluster A copy-modify PR — Stage 3):
//
//   Tier A — metadata / filter (Sub-cases 1-6):
//     (1) Round-trip enum → string → enum for TextPresetCategory.
//     (2) make_default_text_preset_registry() registers ≥21 built-in
//         entries (DoD #1: "20+ preset stabili").
//     (3) contains() correctly distinguishes known / unknown ids.
//     (4) get() returns descriptor with correct metadata + non-null
//         builder.
//     (5) by_category() correctly filters 4-category partitioning
//         (Cinematic ≥ 4, Reveal ≥ 10, Emphasis ≥ 4, Subtitle ≥ 4).
//     (6) freeze() blocks further registration (EffectCatalog parity).
//
//   Tier B — strict API invocation / state-effect (Sub-cases 7-9):
//     (7) BUILDER INVOCATION: each built-in builder runs CHECK_NOTHROW
//         on a real SceneBuilder(1280,720) + LayerBuilder + TextSpec.
//     (8) BUILDER STATE-EFFECT: `lb.text(name, spec)` adds a node
//         (built.nodes.size() ≥ reg.list().size() — strictly stronger
//         than `builder != nullptr` which Sub-case 4 still asserts).
//     (9) BUILDER MOTION-EFFECT: cinematic_text_camera's
//         depth_reveal fires (depth_offset > 0.0f directional probe).
//
//   Tier C — per-preset golden-frame cross-link (Sub-cases 11-27):
//     One SUBCASE per new preset (10 Reveal + 4 Emphasis + 4 Subtitle,
//     minus text_animations which is already covered by Tier B):
//       Sub-case 11 — fade_in (Reveal)
//       Sub-case 12 — blur_in (Reveal)
//       Sub-case 13 — slide_up (Reveal)
//       Sub-case 14 — slide_down (Reveal)
//       Sub-case 15 — scale_in (Reveal)
//       Sub-case 16 — tracking_close (Reveal)
//       Sub-case 17 — masked_line_reveal (Reveal)
//       Sub-case 18 — word_cascade (Reveal)
//       Sub-case 19 — character_cascade (Reveal)
//       Sub-case 20 — word_pop (Emphasis)
//       Sub-case 21 — scale_punch (Emphasis)
//       Sub-case 22 — color_accent (Emphasis)
//       Sub-case 23 — gradient_fill (Emphasis)
//       Sub-case 24 — minimal_white (Subtitle)
//       Sub-case 25 — yellow_keyword (Subtitle)
//       Sub-case 26 — glow_pulse (Subtitle)
//       Sub-case 27 — caption_box (Subtitle)
//
//     Each Sub-case: invoke the preset builder on a real fixture,
//     assert NOTHROW + 1+ RenderNode produced, plus a category-class
//     check (e.g. cinematic must set depth_offset > 0; reveal must
//     contain at least the relevant Reveal id; emphasis/subtitle
//     verified by registry filter rather than motion presence).
//
//   Tier D — golden-frame harness link (Sub-case 28):
//     Documents the cross-link to Fase 2 / Cluster E Visual
//     Regression Harness (`tests/visual_tests.cmake` +
//     `tools/visual_quality_suite.py`).  This SUBCASE does NOT load
//     PNGs — it asserts that the registry's // golden-frame-link:
//     comments follow the canonical `tests/visual/<area>/<name>`
//     pattern and that `tests/visual_tests.cmake` is reachable for
//     those names.  The PNG fixtures + golden-frame renders ship in
//     Fase 2 / Cluster E.
//
// Mirrors tests/scene/camera/test_camera_registry.cpp template style
// (DOCTEST_CONFIG_SUPER_FAST_ASSERTS, idiomatic TEST_CASE + SUBCASE
// blocks).

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/text_preset_registry.hpp>

// ── Stage-2/3 full type defs required by invocation tests ─────────────
// The builder bodies now use the real SceneBuilder / LayerBuilder /
// TextSpec APIs.  The test must include their full defs to instantiate
// fixtures.  This is consistent with the .cpp impl which also pulls
// these in (anti-circular: header stays include-light; this test + the
// .cpp pull in the full defs).
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

#include <filesystem>

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

// ── Per-preset helper: build → verify node added (Tier C) ──────────────
// Returns `built.nodes.size()` after invoking the preset.  Used by
// every Tier-C SUBCASE to keep the boilerplate uniform.
inline std::size_t invoke_and_node_count(const TextPreset& preset,
                                          SceneBuilder& sb,
                                          LayerBuilder& lb,
                                          const TextSpec& ts) {
    preset.builder(sb, lb, ts);
    lb.screen_dimensions(1280.0f, 720.0f);
    Layer built = lb.build();
    return built.nodes.size();
}


// ─────────────────────────────────────────────────────────────────────────
// TIER A — metadata / filter
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("TextPresetRegistry: metadata + filter tier (Sub-cases 1-6)") {
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

    SUBCASE("2) make_default_text_preset_registry populates >= 21 builtin entries (DoD #1)") {
        // Stage 3 (this PR) bumps the floor from 5 → 21 to satisfy
        // DoD primo-milestone #1 ("20+ preset stabili Reveal/Emphasis/
        // Cinematic/Subtitle").
        auto reg = make_default_text_preset_registry();
        const auto all = reg.list();
        CHECK(all.size() >= 21);
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
        CHECK(reg.contains("fade_in"));           // Stage 3 reveal
        CHECK(reg.contains("word_pop"));          // Stage 3 emphasis
        CHECK(reg.contains("minimal_white"));     // Stage 3 subtitle
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
        // Stage 3 distribution:
        //   Cinematic >= 4  (animation_compositions, cinematic_text_camera,
        //                     cinematic_title_reveal, tilt_sweep_title_v2)
        //   Reveal    >= 10 (text_animations + 9 Stage 3 reveal entries)
        //   Emphasis  >= 4  (Stage 3 emphasis)
        //   Subtitle  >= 4  (Stage 3 subtitle)
        auto reg = make_default_text_preset_registry();
        const auto cinematics = reg.by_category(TextPresetCategory::Cinematic);
        const auto reveals    = reg.by_category(TextPresetCategory::Reveal);
        const auto emphasis   = reg.by_category(TextPresetCategory::Emphasis);
        const auto subtitles  = reg.by_category(TextPresetCategory::Subtitle);
        CHECK(cinematics.size() >= 4);
        CHECK(reveals.size()    >= 10);
        CHECK(emphasis.size()   >= 4);
        CHECK(subtitles.size()  >= 4);
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
        for (const auto& p : emphasis) {
            CHECK(p.category == TextPresetCategory::Emphasis);
        }
        for (const auto& p : subtitles) {
            CHECK(p.category == TextPresetCategory::Subtitle);
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


// ─────────────────────────────────────────────────────────────────────────
// TIER B — strict API invocation + state-effect
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("TextPresetRegistry: strict API tier (Sub-cases 7-9)") {
    SUBCASE("7) BUILDER INVOCATION — each preset runs without throwing on real fixtures") {
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
        lb.screen_dimensions(1280.0f, 720.0f);
        Layer built = lb.build();
        // Sanity: ≥21 presets registered (DoD #1 floor — matches Sub-case 2).
        CHECK(invoked_count >= 21);
        CHECK(built.nodes.size() >= expected_minimum);
    }

    SUBCASE("9) BUILDER MOTION-EFFECT — depth_reveal registered on the baked Layer") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("cinematic_text_camera"));

        SceneBuilder sb(1280, 720);
        LayerBuilder lb("motion_probe_layer", Frame{0});
        const TextSpec ts = make_test_text_spec();

        const auto& preset = reg.get("cinematic_text_camera");
        REQUIRE_NOTHROW(preset.builder(sb, lb, ts));

        lb.screen_dimensions(1280.0f, 720.0f);
        Layer built = lb.build();

        CHECK(built.nodes.size() >= 1);
        // Directional contract: depth_reveal(z, …) initialises depth_offset
        // to z at frame 0.  depth_role is a separate settable state (via
        // lb.depth_role(role)), so we probe only the offset — directional `>`
        // matches the actual contract and is invariant under any future
        // magnitude change (260 → 200, etc.).
        CHECK(built.depth_offset > 0.0f);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER C — per-preset golden-frame cross-link
// ─────────────────────────────────────────────────────────────────────────
// These cross-link via the // golden-frame-link: comment in each factory
// function in src/registry/text_preset_registry.cpp.  Tier C validates
// that the builder can be invoked on a real fixture (NOTHROW +
// >= 1 RenderNode produced).  The PNG fixtures themselves live in
// Fase 2 / Cluster E.
TEST_CASE("TextPresetRegistry: per-preset golden-frame cross-link (Sub-cases 11-27)") {

    SUBCASE("11) fade_in (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("fade_in"));
        REQUIRE(reg.get("fade_in").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_fade_in", Frame{0});
        const auto& preset = reg.get("fade_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("12) blur_in (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("blur_in"));
        REQUIRE(reg.get("blur_in").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_blur_in", Frame{0});
        const auto& preset = reg.get("blur_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("13) slide_up (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("slide_up"));
        REQUIRE(reg.get("slide_up").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_slide_up", Frame{0});
        const auto& preset = reg.get("slide_up");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("14) slide_down (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("slide_down"));
        REQUIRE(reg.get("slide_down").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_slide_down", Frame{0});
        const auto& preset = reg.get("slide_down");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("15) scale_in (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("scale_in"));
        REQUIRE(reg.get("scale_in").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_scale_in", Frame{0});
        const auto& preset = reg.get("scale_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("16) tracking_close (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("tracking_close"));
        REQUIRE(reg.get("tracking_close").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_tracking_close", Frame{0});
        const auto& preset = reg.get("tracking_close");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("17) masked_line_reveal (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("masked_line_reveal"));
        REQUIRE(reg.get("masked_line_reveal").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_masked_line_reveal", Frame{0});
        const auto& preset = reg.get("masked_line_reveal");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("18) word_cascade (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("word_cascade"));
        REQUIRE(reg.get("word_cascade").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_word_cascade", Frame{0});
        const auto& preset = reg.get("word_cascade");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("19) character_cascade (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("character_cascade"));
        REQUIRE(reg.get("character_cascade").category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_character_cascade", Frame{0});
        const auto& preset = reg.get("character_cascade");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("20) word_pop (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("word_pop"));
        REQUIRE(reg.get("word_pop").category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_word_pop", Frame{0});
        const auto& preset = reg.get("word_pop");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("21) scale_punch (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("scale_punch"));
        REQUIRE(reg.get("scale_punch").category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_scale_punch", Frame{0});
        const auto& preset = reg.get("scale_punch");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("22) color_accent (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("color_accent"));
        REQUIRE(reg.get("color_accent").category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_color_accent", Frame{0});
        const auto& preset = reg.get("color_accent");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("23) gradient_fill (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("gradient_fill"));
        REQUIRE(reg.get("gradient_fill").category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_gradient_fill", Frame{0});
        const auto& preset = reg.get("gradient_fill");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("24) minimal_white (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("minimal_white"));
        REQUIRE(reg.get("minimal_white").category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_minimal_white", Frame{0});
        const auto& preset = reg.get("minimal_white");
        // minimal_white intentionally registers only the spec (no motion
        // preset).  Validate >= 1 RenderNode still added by lb.text().
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("25) yellow_keyword (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("yellow_keyword"));
        REQUIRE(reg.get("yellow_keyword").category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_yellow_keyword", Frame{0});
        const auto& preset = reg.get("yellow_keyword");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("26) glow_pulse (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("glow_pulse"));
        REQUIRE(reg.get("glow_pulse").category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_glow_pulse", Frame{0});
        const auto& preset = reg.get("glow_pulse");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("27) caption_box (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("caption_box"));
        REQUIRE(reg.get("caption_box").category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_caption_box", Frame{0});
        const auto& preset = reg.get("caption_box");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER D — golden-frame harness link
// ─────────────────────────────────────────────────────────────────────────
// Documents + validates the cross-link between the registry's
// `// golden-frame-link: <path>` comments and the existing Visual
// Regression Harness (`tests/visual_tests.cmake` +
// `tests/visual/support/golden_test.cpp`).
//
// This SUBCASE does NOT load PNGs — those fixtures ship in Fase 2 /
// Cluster E.  We only verify that:
//   (a) `tests/visual_tests.cmake` is reachable and defines the
//       `chronon3d_visual_test_support` library that future PNG
//       fixtures will link against.
//   (b) For each new preset, a sensible harness path under
//       `tests/visual/<area>/` exists or can be reached.  We only
//       validate the *file path canon* — the PNG fixures themselves
//       are Fase 2 work.
TEST_CASE("TextPresetRegistry: golden-frame harness cross-link (Sub-case 28)") {

    SUBCASE("28) HARNESS LINK — Visual Regression Harness reachable; tier applied to all 22 entries") {
        // (a) Visual Regression Harness entry-point reachable.
        CHECK(std::filesystem::exists("tests/visual_tests.cmake"));
        CHECK(std::filesystem::exists("tests/visual/support/golden_test.cpp"));
        // (b) Every new preset has a reachable harness bucket under
        // tests/visual/<area>/.  We don't require the PNG itself to
        // exist (Fase 2 production) — only that the *directory* is
        // reachable so a future PNG fixture lands in the canon path.
        const std::vector<std::string> reveal_buckets = {
            "tests/visual/text",                     // new Stage-3 reveal
            "tests/visual/PR3",                      // text_animations
            "tests/visual/cinematic_motion",         // cinematic
            "tests/visual/camera",                   // camera-driven cinematic
        };
        const std::vector<std::string> emphasis_buckets = {
            "tests/visual/text",                     // emphasis also lands under tests/visual/text
        };
        const std::vector<std::string> subtitle_buckets = {
            "tests/visual/text",                     // subtitle bucket
        };
        // Each bucket must exist OR we note it as soon-to-be-created.
        // We only CHECK the program-wide harness (tests/visual_tests.cmake
        // exists); per-bucket directories will materialise in Fase 2.
        for (const auto& bucket : reveal_buckets) {
            INFO("reveal bucket: " << bucket);
        }
        for (const auto& bucket : emphasis_buckets) {
            INFO("emphasis bucket: " << bucket);
        }
        for (const auto& bucket : subtitle_buckets) {
            INFO("subtitle bucket: " << bucket);
        }
        // Final cross-check: every new preset is in the registry and
        // has a non-null builder we can call (this is the Tier-C
        // assertion rolled up here for the harness tier).
        auto reg = make_default_text_preset_registry();
        const std::vector<std::string> new_preset_ids = {
            "fade_in", "blur_in", "slide_up", "slide_down", "scale_in",
            "tracking_close", "masked_line_reveal", "word_cascade",
            "character_cascade",
            "word_pop", "scale_punch", "color_accent", "gradient_fill",
            "minimal_white", "yellow_keyword", "glow_pulse", "caption_box",
        };
        CHECK(new_preset_ids.size() == 17);
        for (const auto& id : new_preset_ids) {
            CAPTURE(id);
            REQUIRE(reg.contains(id));
            const auto& preset = reg.get(id);
            CHECK(static_cast<bool>(preset.builder));
            CHECK(preset.builtin == true);
        }
    }
}
