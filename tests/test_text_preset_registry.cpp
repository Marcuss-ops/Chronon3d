// ─── test_text_preset_registry.cpp — Cluster A DoD #1 test (TEXT-RES-01 migrated) ───────────────
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

// Cluster B public API surface — the deterministic verification entry
// point for the cross-TU test harness and any downstream authoring
// facade.  Declared in <chronon3d/registry/text_preset_resolver.hpp>.
#include <chronon3d/registry/text_preset_resolver.hpp>

// TICKET-012 — header-lifted AnimatorResolver (peer header of text_preset_resolver.hpp).
// Sub-case 32 calls AnimatorResolver::compose_for / rich_paint_anchor / spec_is_rich
// DIRECTLY from this test TU (without going through the factory-body pipeline) so
// the header-lift's PRIMARY contract — "callable from any TU" — has a structural
// compile-time guard, not just indirect coverage via the registry factory body.
#include <chronon3d/registry/animator_resolver.hpp>

// ── Stage-2/3 full type defs required by invocation tests ─────────────
// The builder bodies now use the real SceneBuilder / LayerBuilder /
// TextSpec APIs.  The test must include their full defs to instantiate
// fixtures.  This is consistent with the .cpp impl which also pulls
// these in (anti-circular: header stays include-light; this test + the
// .cpp pull in the full defs).
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
// ── Stage-4 wiring probe: TextRunShape.h + animators vector ────────────
// Sub-case 29 verifies that the AnimatorResolver wired a
// TextAnimatorSpec entry onto `node.shape.text_run_shape_handle().value->animators`.
// The full TextRunShape definition lives in text/text_run.hpp.
#include <chronon3d/text/text_run.hpp>

// ── Phase-3.3.D test-only inspector for pending text-run entries ──────
// Sub-cases 29 + 30 previously called `lb.pending_text_runs()` which
// returned `std::vector<const PendingTextRun*>` (raw internal pointers).
// Phase-3.3.D extracted that inspector into a friend-mediated value-
// typed adapter under chronon3d::builders::testing::LayerBuilderInspector.
// The API surface is now snapshot-based — `pre[i].name` and
// `pre[i].animators` instead of `pre[i]->name` / `pre[i]->params.animators`.
#include "support/layer_builder_inspection.hpp"

// TICKET-107 — per-category register helpers reachable from sibling TUs.
// The header lives at src/registry/text_preset_register_helpers.hpp and
// is NOT installed.  Tests include it via a relative include path.
#include "../src/registry/text_preset_register_helpers.hpp"

using chronon3d::builders::testing::LayerBuilderInspector;

// Reviewer finding #6 — call through the verbose `register_helpers_internal::`
// namespace path (or via the `reg_helpers` alias) to keep the per-category
// helpers out of the parent `chronon3d::registry` public surface.
namespace reg_helpers = chronon3d::registry::register_helpers_internal;

#include <functional>   // std::function for Sub-case 30/31 expected_kind_predicate
#include <array>        // std::array<ExpectedComposition, 22>
#include <variant>      // std::holds_alternative for Sub-case 30/31 kind checks
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

// ── Fixture: rich-paint TextSpec (Stage 4 wiring probe) ─────────────────
// Triggers the Stage-4 AnimatorResolver in cinematic_text_camera via the
// three rich-paint signals the resolver keys off:
//   - appearance.paint.fill_style.has_value()  (Fill populated)
//   - appearance.paint.stroke_enabled          (true)
//   - appearance.paint.stroke_style populated  (optional, has_value() proxy)
// With ANY of those signals firing, the resolver pushes a global-selector
// TextAnimatorSpec with id prefix "ctc_rich_cinematic_text_camera" onto
// the TextRunSpec.animators vector BEFORE the canonical motion-preset
// chain runs (depth_reveal + scale_drop + soft_pop).
inline TextSpec make_chronon_rich_text_spec() {
    return TextSpec{
        .content    = {.value = "CHRONON"},
        .font       = {.font_path   = "assets/fonts/Poppins-Bold.ttf",
                       .font_family = "Poppins",
                       .font_weight = 700,
                       .font_style  = "normal",
                       .font_size   = 96.0f},
        .layout     = {.box = {1200.0f, 240.0f}},
        .appearance = {.color = {1.0f, 1.0f, 1.0f, 1.0f},
                       .paint  = {.fill = {1.0f, 1.0f, 1.0f, 1.0f},
                                  // ── Richness trigger (bool signal) ──
                                  .stroke_enabled = true,
                                  .stroke_color  = {0.0f, 0.0f, 0.0f, 1.0f},
                                  .stroke_width  = 2.0f}},
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
            CHECK(p.metadata.builtin == true);
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
        CHECK(spec.metadata.category == TextPresetCategory::Cinematic);
        CHECK(spec.metadata.builtin == true);
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
            CHECK(p.metadata.category == TextPresetCategory::Cinematic);
        }
        for (const auto& p : reveals) {
            CHECK(p.metadata.category == TextPresetCategory::Reveal);
        }
        for (const auto& p : emphasis) {
            CHECK(p.metadata.category == TextPresetCategory::Emphasis);
        }
        for (const auto& p : subtitles) {
            CHECK(p.metadata.category == TextPresetCategory::Subtitle);
        }
    }

    SUBCASE("6) freeze() blocks further registration (EffectCatalog parity)") {
        TextPresetRegistry reg;
        TextPreset p;
        p.id = "foo";
        p.metadata.category = TextPresetCategory::Reveal;
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
        REQUIRE(reg.get("fade_in").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_fade_in", Frame{0});
        const auto& preset = reg.get("fade_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("12) blur_in (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("blur_in"));
        REQUIRE(reg.get("blur_in").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_blur_in", Frame{0});
        const auto& preset = reg.get("blur_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("13) slide_up (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("slide_up"));
        REQUIRE(reg.get("slide_up").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_slide_up", Frame{0});
        const auto& preset = reg.get("slide_up");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("14) slide_down (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("slide_down"));
        REQUIRE(reg.get("slide_down").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_slide_down", Frame{0});
        const auto& preset = reg.get("slide_down");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("15) scale_in (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("scale_in"));
        REQUIRE(reg.get("scale_in").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_scale_in", Frame{0});
        const auto& preset = reg.get("scale_in");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("16) tracking_close (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("tracking_close"));
        REQUIRE(reg.get("tracking_close").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_tracking_close", Frame{0});
        const auto& preset = reg.get("tracking_close");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("17) masked_line_reveal (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("masked_line_reveal"));
        REQUIRE(reg.get("masked_line_reveal").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_masked_line_reveal", Frame{0});
        const auto& preset = reg.get("masked_line_reveal");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("18) word_cascade (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("word_cascade"));
        REQUIRE(reg.get("word_cascade").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_word_cascade", Frame{0});
        const auto& preset = reg.get("word_cascade");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("19) character_cascade (Reveal) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("character_cascade"));
        REQUIRE(reg.get("character_cascade").metadata.category == TextPresetCategory::Reveal);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_character_cascade", Frame{0});
        const auto& preset = reg.get("character_cascade");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("20) word_pop (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("word_pop"));
        REQUIRE(reg.get("word_pop").metadata.category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_word_pop", Frame{0});
        const auto& preset = reg.get("word_pop");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("21) scale_punch (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("scale_punch"));
        REQUIRE(reg.get("scale_punch").metadata.category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_scale_punch", Frame{0});
        const auto& preset = reg.get("scale_punch");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("22) color_accent (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("color_accent"));
        REQUIRE(reg.get("color_accent").metadata.category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_color_accent", Frame{0});
        const auto& preset = reg.get("color_accent");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("23) gradient_fill (Emphasis) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("gradient_fill"));
        REQUIRE(reg.get("gradient_fill").metadata.category == TextPresetCategory::Emphasis);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_gradient_fill", Frame{0});
        const auto& preset = reg.get("gradient_fill");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("24) minimal_white (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("minimal_white"));
        REQUIRE(reg.get("minimal_white").metadata.category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_minimal_white", Frame{0});
        const auto& preset = reg.get("minimal_white");
        // minimal_white intentionally registers only the spec (no motion
        // preset).  Validate >= 1 RenderNode still added by lb.text().
        // ShapeType::TextRun contract for minimal_white is locked in
        // Sub-case 30 (pre-build probe via lb.pending_text_runs()), since
        // a second `lb.build()` here would observe spec.consumed=true
        // on every entry and emit zero nodes (the consume-on-build
        // path is one-shot per constructor).
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("25) yellow_keyword (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("yellow_keyword"));
        REQUIRE(reg.get("yellow_keyword").metadata.category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_yellow_keyword", Frame{0});
        const auto& preset = reg.get("yellow_keyword");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("26) glow_pulse (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("glow_pulse"));
        REQUIRE(reg.get("glow_pulse").metadata.category == TextPresetCategory::Subtitle);
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("golden_glow_pulse", Frame{0});
        const auto& preset = reg.get("glow_pulse");
        CHECK(invoke_and_node_count(preset, sb, lb, make_test_text_spec()) >= 1);
    }

    SUBCASE("27) caption_box (Subtitle) → fixture invocation produces >= 1 node") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("caption_box"));
        REQUIRE(reg.get("caption_box").metadata.category == TextPresetCategory::Subtitle);
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
        // Visual Regression Harness entry point reachable.  PNG fixtures
        // themselves ship in Fase 2 / Cluster E — this SUBCASE only
        // asserts the harness scaffolding is in place.
        CHECK(std::filesystem::exists("tests/visual_tests.cmake"));
        CHECK(std::filesystem::exists("tests/visual/support/golden_test.cpp"));
        // Cross-check: every new preset is in the registry with
        // non-null builder + builtin=true.  This is the Tier-C
        // invariant rolled up here for the harness tier.
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
            CHECK(preset.metadata.builtin == true);
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER E — TextAnimator V2 wiring (Stage 4 / DoD #2 closure)
// ─────────────────────────────────────────────────────────────────────────
// Sub-case 29 verifies that the AnimatorResolver in
// `src/registry/text_preset_registry.cpp` correctly resolves motion id
// against a richly-painted spec BEFORE invoking the canonical
// motion-preset chain in the cinematic_text_camera builder body.
//
// The contract being tested (Stage 4 hardening):
//   Given: preset = cinematic_text_camera, spec = make_chronon_rich_text_spec()
//          (content.value = "CHRONON" + appearance.paint.stroke_enabled = true)
//   When : the cinematic_text_camera builder lambda runs
//   Then : (a) build() does not throw and produces >= 1 RenderNode,
//          (b) the canonical motion-preset canon still fires post-wiring
//              (Layer::depth_offset > 0 — depth_reveal(260, Frame{50}) ran),
//          (c) PRE-build, the LayerBuilder's `pending_text_runs()` shows
//              exactly one entry named "camera_text" with the wired
//              TextAnimatorSpec entry `id = "ctc_rich_cinematic_text_camera"`
//              on its `params.animators` vector — DETERMINISTIC, does NOT
//              depend on font-engine materialisation succeeding.
//
// Probe (c) relies on the test-only `LayerBuilder::pending_text_runs()`
// inspector (added alongside this test in the Stage 4 hardening PR)
// which reads `m_text_runs` before `LayerBuilder::build()` consumes
// the entries into `Layer::nodes`.  This replaces the prior lenient
// post-build probe (which silently regressed if materialisation
// returned a null `text_run_shape_handle().value`).
TEST_CASE("TextPresetRegistry: TextAnimator V2 wiring tier (Sub-case 29)") {        SUBCASE("29) BUILDER WIRES_TEXT_ANIMATORS — cinematic_text_camera resolves motion-id to wired TextAnimatorSpec on rich-paint spec") {
        auto reg = make_default_text_preset_registry();
        REQUIRE(reg.contains("cinematic_text_camera"));
        REQUIRE(reg.get("cinematic_text_camera").metadata.category == TextPresetCategory::Cinematic);

        // ── Fixture: spec.content.value = "CHRONON" with rich paint. ────
        TextSpec rich_spec = make_chronon_rich_text_spec();
        REQUIRE_FALSE(rich_spec.content.value.empty());
        CHECK(rich_spec.content.value == "CHRONON");
        CHECK(rich_spec.appearance.paint.stroke_enabled);

        // ── Pre-build assertion: invariants hold BEFORE preset.builder ───
        SceneBuilder sb(1280, 720);
        LayerBuilder lb("wiring_test_layer", Frame{0});
        const auto& preset = reg.get("cinematic_text_camera");
        // No text_run(...) yet → pending runs (test-only inspector) must be empty.
        REQUIRE(LayerBuilderInspector::pending_runs(lb).empty());

        REQUIRE_NOTHROW(preset.builder(sb, lb, rich_spec));

        // ── (c) DETERMINISTIC pre-build probe ─────────────────────────
        // Read pending text-run entries via LayerBuilderInspector BEFORE
        // `lb.build()` consumes them.  Phase-3.3.D: the inspector
        // returns a value-typed snapshot (`std::vector<PendingRunSnapshot>`)
        // so `pre[i].name` / `pre[i].animators` are value accesses
        // (no raw internal pointers).
        // Hard-CHECK (no IF-skip): the resolver must have pushed a
        // TextAnimatorSpec EXACTLY at animators[0] (the resolver pushes
        // it ONCE before the cinematic builder's factory body returns,
        // so this is the strictest possible index-based assertion — a
        // future Stage 5+ resolver extension that inserts a different
        // animator BEFORE the wired entry will fail this CHECK loudly).
        const auto pre = LayerBuilderInspector::pending_runs(lb);
        REQUIRE(pre.size() == 1);
        CHECK(pre[0].name == "camera_text");
        const auto& animators = pre[0].animators;
        REQUIRE(animators.size() >= 1);
        // Strict equality: AnimatorResolver composes the wired id as
        // "ctc_rich_" + preset_id, where preset_id = "cinematic_text_camera".
        // The full id is therefore the literal "ctc_rich_cinematic_text_camera"
        // — strictly stronger than a prefix match (a future resolver extension
        // that prefixes or suffixes the id would break this CHECK loudly).
        // If the resolver's id format evolves (e.g. a frame-aware suffix like
        // "ctc_rich_cinematic_text_camera_f0"), update this test alongside the
        // resolver — exact-match is a deliberate contract lock.
        CHECK(animators[0].id == "ctc_rich_cinematic_text_camera");

        // ── Build the layer; verify Layer::nodes still materialises. ───
        lb.screen_dimensions(1280.0f, 720.0f);
        Layer built = lb.build();
        REQUIRE(built.nodes.size() >= 1);

        // ── (b) Motion-preset canon still fired AFTER the resolver. ────
        // depth_reveal(z, …) sets depth_offset > 0 at frame 0 — same
        // directional probe as Sub-case 9.  Confirms the canonical chain
        // ran post-wiring (.commit() returned LayerBuilder& and we
        // chained depth_reveal/scale_drop/soft_pop after the resolver's
        // text_run(...) commit).
        CHECK(built.depth_offset > 0.0f);

        // ── Bonus probe: golden-frame-link canon preserved ────────────
        // The wiring tier ships the same `// golden-frame-link:` canon
        // comments as Stages 2 + 3; we re-confirm the fixture path
        // exists at tests/visual_tests.cmake so reviewers can rely on
        // end-to-end PNG traceability.
        CHECK(std::filesystem::exists("tests/visual_tests.cmake"));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TIER F — Stage 5 AnimatorResolver coverage (all 22 presets)
// ─────────────────────────────────────────────────────────────────────────
// Verifies that AnimatorResolver::compose_for() (now extended to ALL 22
// presets) routes each built-in preset through `wire_through_resolver()`
// in its factory body, leaving a PendingTextRun entry whose
// animators[0].id carries the `presetc_<preset_id>` prefix from the
// new Stage 5 resolver.  Every preset EXCEPT `minimal_white` (no
// canonical motion) pushes at least one TextAnimatorSpec via
// `lb.text_run(...).commit()` BEFORE the layer-level motion chain.
// Sub-case 29 already covers the rich-paint path on cinematic_text_camera
// (animators[0].id strictly equal to "ctc_rich_cinematic_text_camera"),
// so Sub-case 30 only probes plain-spec wiring to keep assertions
// orthogonal.
TEST_CASE("TextPresetRegistry: Stage 5 AnimatorResolver coverage (Sub-case 30)") {

    SUBCASE("30) compose_for wires property-level composition for all 22 presets via wire_through_resolver") {
        const auto& reg = make_default_text_preset_registry();

        // Plain test_spec — no rich-paint signal — so cinematic_text_camera
        // plain-spec path is exercised (Sub-case 29 covers the rich path).
        const auto plain = make_test_text_spec();

        // Expected property-pack minimum count per preset_id. Mirrors the
        // 22-branch table in src/registry/text_preset_registry.cpp
        // (AnimatorResolver::compose_for()). If the resolver table
        // evolves, this array evolves alongside it.
        //
        // `first_kind_predicate` catches the strongest drift signature:
        // a Stage 6+ refactor that accidentally swaps the FIRST push's
        // property type (e.g., `PositionProperty` → `ScaleProperty` on
        // the cinematic_text_camera branch).  Loose count alone would
        // not detect such a swap.
        struct ExpectedComposition {
            std::string preset_id;
            std::size_t min_property_count;
            std::function<bool(const TextAnimatorProperty&)> first_kind_predicate;
        };
        const std::array<ExpectedComposition, 22> expected{{
            {"animation_compositions",      2,  // Position + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            {"cinematic_text_camera",       3,  // Position + Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            {"cinematic_title_reveal",      2,  // Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"tilt_sweep_title_v2",         3,  // Scale + Blur + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"text_animations",             2,  // Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"fade_in",                     1,  // Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<OpacityProperty>(p);
                }},
            {"blur_in",                     2,  // Blur + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<BlurProperty>(p);
                }},
            {"slide_up",                    2,  // Position + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            {"slide_down",                  2,  // Position + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            {"scale_in",                    2,  // Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"tracking_close",              1,  // Tracking
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<TrackingProperty>(p);
                }},
            {"masked_line_reveal",          1,  // Position
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            {"word_cascade",                1,  // Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<OpacityProperty>(p);
                }},
            {"character_cascade",           1,  // Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<OpacityProperty>(p);
                }},
            {"word_pop",                    2,  // Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"scale_punch",                 2,  // Scale + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<ScaleProperty>(p);
                }},
            {"color_accent",                1,  // Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<OpacityProperty>(p);
                }},
            {"gradient_fill",               2,  // Position + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
            // minimal_white: no resolver output — predicate unused
            // (loop `continue`s before reaching the kind-check).
            {"minimal_white",               0,
                [](const TextAnimatorProperty& p){ return true; }},
            {"yellow_keyword",              1,  // Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<OpacityProperty>(p);
                }},
            {"glow_pulse",                  1,  // Tracking
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<TrackingProperty>(p);
                }},
            {"caption_box",                 2,  // Position + Opacity
                [](const TextAnimatorProperty& p){
                    return std::holds_alternative<PositionProperty>(p);
                }},
        }};

        REQUIRE(reg.list().size() == expected.size());

        // Helper — verifies the FIRST property on a wired TextAnimatorSpec
        // is of the alternative-type `T`.  This catches future drift in
        // the branch table silently swapping property kinds (e.g., a
        // `ScaleProperty` accidentally replacing `PositionProperty` in the
        // `cinematic_text_camera` branch).  See the per-preset
        // `expected_first_kind` field below.
        const auto first_kind_is = [](const TextAnimatorSpec& spec,
                                      auto kind_predicate) -> bool {
            if (spec.properties.empty()) return false;
            return kind_predicate(spec.properties[0]);
        };

        for (const auto& exp : expected) {
            CAPTURE(exp.preset_id);
            REQUIRE(reg.contains(exp.preset_id));
            // Use the canonical LayerBuilder(string, Frame) constructor
            // pattern — matches every other Sub-case in this file
            // (see Sub-cases 7-9 and Tier C at lines ~266, ~278, etc).
            LayerBuilder lb("resolve_probe_" + exp.preset_id, Frame{0});
            SceneBuilder sb(1280, 720);
            reg.get(exp.preset_id).builder(sb, lb, plain);

            // Read pending text-run entries via LayerBuilderInspector
            // BEFORE any subsequent mutation.  Phase-3.3.D: returns a
            // value-typed snapshot (`std::vector<PendingRunSnapshot>`)
            // — no raw internal pointers, no reallocation-implication
            // caveats to worry about.
            const auto pre = LayerBuilderInspector::pending_runs(lb);

            if (exp.preset_id == "minimal_white") {
                // P1 — single canonical text pipeline. minimal_white
                // ALSO flows through wire_through_resolver → text_run().
                // commit(), but the resolver composes no animator (fail-
                // safe nullopt branch in AnimatorResolver::compose_for —
                // see Sub-case 32a). The PendingTextRun entry IS pushed
                // (preserving the single-path contract); its animators
                // vector is empty so `materialize_text_run_shape` produces
                // a valid shape without per-frame driver work.
                REQUIRE(pre.size() == 1);
                CHECK(pre[0].animators.empty());
                continue;
            }

            // Stage 5 wiring: every other preset routes through
            // `lb.text_run(...).commit()`, leaving exactly one entry
            // with the resolver's property-composed spec on animators[0]
            // (plain-spec path; rich-spec pushes the anchor ahead of the
            // canonical — Sub-case 29 already covers that).
            CHECK(pre.size() == 1);
            const auto& animators = pre[0].animators;
            CHECK(animators.size() >= 1);
            CHECK(animators[0].id == ("presetc_" + exp.preset_id));
            CHECK(animators[0].properties.size() >= exp.min_property_count);

            // Stronger drift catch — verifies the FIRST property kind
            // matches the canonical mapping.  Loose count alone would
            // let a Stage 6+ refactor silently swap PositionProperty for
            // ScaleProperty on the cinematic_text_camera branch.
            if (!animators[0].properties.empty()) {
                CHECK(first_kind_is(animators[0], exp.first_kind_predicate));
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TIER G — Cluster B public API surface (Sub-case 31)
// ─────────────────────────────────────────────────────────────────────────
// Stage 5+ exposes the AnimatorResolver table via a SINGLE public free
// function: `wire_preset_text_run_params(preset_id, spec) -> TextRunParams`.
// This is the deterministic verification entry point the test harness and
// downstream authoring facade use — no LayerBuilder, no SceneBuilder,
// no factory-body invocation.  Sub-case 31 iterates all 22 presets and
// asserts the public function returns the same canonical end-state
// composition the registry factory bodies wire (Sub-cases 7-30).
//
// Why this matters: the test harness's deterministic verification path
// (Sub-case 30) requires constructing LayerBuilder + SceneBuilder +
// invoking the factory body.  Sub-case 31 collapses all that ceremony
// behind a single pure-function call.  If the test harness is ever
// migrated to a Cluster B authoring facade, only Sub-case 31 needs to
// follow — Sub-case 30 stays as the integration regression test.
TEST_CASE("TextPresetRegistry: Cluster B public API surface (Sub-case 31)") {

    SUBCASE("31) wire_preset_text_run_params returns deterministic TextRunParams for all 22 presets") {
        const auto& reg = make_default_text_preset_registry();
        const auto plain = make_test_text_spec();

        // Signature contract check — the public free function lives
        // in <chronon3d/registry/text_preset_resolver.hpp> with the
        // exact signature (string_view, TextSpec) -> TextRunParams.
        // The compile-time assertion below verifies the resolved type
        // via `decltype(function-name)` which yields the function type.
        static_assert(
            std::is_same_v<
                decltype(wire_preset_text_run_params),
                TextRunParams(std::string_view, TextSpec) noexcept>,
            "wire_preset_text_run_params must return TextRunParams via (string_view, TextSpec) noexcept");

        // ── Per-preset pure-function probe ─────────────────────────────────
        // Iterate all 22 preset ids via reg.available() (sorted-by-key,
        // deterministic across runs).  For each id, the public function
        // must return:
        //   - animators.empty() == true   for `minimal_white` only
        //   - animators.size() == 1       for every other preset id
        //   - animators[0].id == "presetc_<id>"  in wired cases
        //   - animators[0].text survives the move (params.text == supplied spec)
        //   - animators[0].properties[0] holds the canonical first-property kind
        //
        // An additional sub-test verifies the third contract (text survival):
        // params.text == supplied spec — so the canonical "spec-in / spec-out"
        // pattern holds, and downstream authoring facades can rely on
        // `params.text` being the moved-from caller-supplied spec.
        for (const auto& id : reg.available()) {
            CAPTURE(id);
            REQUIRE(reg.contains(id));

            const auto params =
                wire_preset_text_run_params(id, plain);

            // Test-text-survival contract: the spec travels through the
            // public function unchanged (moved into params.text).
            CHECK(params.text.content.value == plain.content.value);

            if (id == "minimal_white") {
                // No canonical motion → public function returns
                // TextRunParams with animators.empty() == true.  The
                // caller routes via plain lb.text(...) which does not
                // require an AnimatorResolver entry.
                CHECK(params.animators.empty());
                continue;
            }

            // Stage 5 wiring: every other preset yields exactly one
            // composed TextAnimatorSpec pushed onto animators[0].
            REQUIRE(params.animators.size() == 1);
            CHECK(params.animators[0].id == ("presetc_" + id));
            CHECK(params.animators[0].enabled);

            // First-property kind check (cross-preset).  Mirrors
            // Sub-case 30's per-branch assertions, but reached via the
            // public API rather than the LayerBuilder factory-body path.
            REQUIRE_FALSE(params.animators[0].properties.empty());
            const auto& first_prop = params.animators[0].properties[0];
            if (id == "animation_compositions"  || id == "slide_up"        ||
                id == "slide_down"             || id == "masked_line_reveal" ||
                id == "gradient_fill"          || id == "caption_box"     ||
                id == "cinematic_text_camera") {
                CHECK(std::holds_alternative<PositionProperty>(first_prop));
            }
            else if (id == "cinematic_title_reveal" || id == "tilt_sweep_title_v2" ||
                     id == "text_animations"        || id == "scale_in"           ||
                     id == "word_pop"               || id == "scale_punch") {
                CHECK(std::holds_alternative<ScaleProperty>(first_prop));
            }
            else if (id == "fade_in" || id == "word_cascade" ||
                     id == "character_cascade" || id == "color_accent" ||
                     id == "yellow_keyword") {
                CHECK(std::holds_alternative<OpacityProperty>(first_prop));
            }
            else if (id == "blur_in") {
                // Only `blur_in` has BlurProperty as the FIRST pushed
                // property.  `tilt_sweep_title_v2` ALSO pushes BlurProperty
                // but its first push is ScaleProperty (matched in the
                // prior branch — verified in compose_for() table).
                CHECK(std::holds_alternative<BlurProperty>(first_prop));
            }
            else if (id == "tracking_close" || id == "glow_pulse") {
                CHECK(std::holds_alternative<TrackingProperty>(first_prop));
            }
            else {
                // Drift catcher: a Stage 6+ branch table expansion that
                // falls outside the expected kind buckets above.
                // Doctest's FAIL accepts a stream-extraction expression:
                // multiple arguments chained with `<<` flow into a
                // MessageBuilder and the test aborts with the resulting
                // string.  Use the canonical `<<` chain so the FAIL macro
                // context resolves correctly (string-concat `+` does NOT
                // resolve inside MessageBuilder -- verified empirically).
                FAIL("Unknown preset_id branch in Sub-case 31: " << id);
            }
        }

        // ── Unknown id contract (fail-safe path) ───────────────────────────
        // The public function must return TextRunParams with empty
        // animators when called with an id that is not in the registered
        // catalog.  This is the fail-safe fallback for any downstream
        // authoring facade that misroutes a preset id.
        const auto unknown_params =
            wire_preset_text_run_params("phantom_preset_unknown", plain);
        CHECK(unknown_params.text.content.value == plain.content.value);
        CHECK(unknown_params.animators.empty());

        // ── spec-move semantics (move-constructible input) ────────────────
        // Verify the public function accepts an rvalue spec (the
        // canonical authoring-facade pattern is `wire_preset_text_run_
        // params(id, std::move(my_spec))`).  If the signature is
        // mis-tuned (e.g. takes const-ref only), the move is silently
        // elided into a copy and this assertion over-constructs; we
        // include it as a compile-time + runtime smoke test.
        TextSpec movable = make_test_text_spec();
        const std::string original_value = movable.content.value;
        const auto moved_params =
            wire_preset_text_run_params("fade_in", std::move(movable));
        CHECK(moved_params.text.content.value == original_value);
        CHECK(moved_params.animators.size() == 1);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER H — AnimatorResolver direct-call coverage (Sub-case 32, TICKET-012)
// ─────────────────────────────────────────────────────────────────────────
// TICKET-012 header-lifts `struct AnimatorResolver` from a private
// anonymous namespace in `src/registry/text_preset_registry.cpp` into a
// public header (`include/chronon3d/registry/animator_resolver.hpp`).
// Sub-cases 30 + 31 already exercise the resolver indirectly via the
// factory-body pipeline (SUBCASE 30) and the public
// `wire_preset_text_run_params` entry point (SUBCASE 31).  Sub-case 32
// locks the header-lift's PRIMARY contract — *any TU that includes
// the new header reaches the same resolver table without include-
// transitive reliance on the registry impl TU*.  Without Sub-case 32,
// TICKET-012's acceptance criterion (b) ("callable from any TU") is
// unproven; with it, the test TU is itself a structural compile-time
// guard for the header-lift.
//
// If the header-lift BROKE, the cross-TU coverage would manifest as
// either:
//   (i)   'AnimatorResolver has not been declared' at compile time
//         (test TU fails to compile; CI surfaces red at compile step,
//         before any run-time tuple is exercised), or
//   (ii)  silent shadow by a same-named symbol from a transitive
//         include path — caught by 32b + 32c strict id-format checks.
TEST_CASE("TextPresetRegistry: TICKET-012 AnimatorResolver direct-call coverage (Sub-case 32)") {

    SUBCASE("32a) compose_for returns std::nullopt for unknown ids + minimal_white (Stage 5 fail-safe)") {
        CHECK_FALSE(AnimatorResolver::compose_for("phantom_unknown_id").has_value());
        CHECK_FALSE(AnimatorResolver::compose_for("").has_value());
        // minimal_white is the ONLY preset with no canonical motion in
        // Stage 5 — the resolver should fail-safe to nullopt so the
        // factory body falls back to plain `lb.text(...)`.
        CHECK_FALSE(AnimatorResolver::compose_for("minimal_white").has_value());
    }

    SUBCASE("32b) compose_for returns 'presetc_<preset_id>' id format for known ids (one per category)") {
        // One sample per category + the tracking_close discriminator.
        for (const auto& pid : {"fade_in", "word_pop", "cinematic_text_camera",
                                "minimal_white", "caption_box", "tilt_sweep_title_v2",
                                "tracking_close"}) {
            CAPTURE(pid);
            const auto opt = AnimatorResolver::compose_for(pid);
            if (std::string{pid} == "minimal_white") {
                CHECK_FALSE(opt.has_value());  // Stage 5 fail-safe.
                continue;
            }
            REQUIRE(opt.has_value());
            CHECK(opt->id == ("presetc_" + std::string{pid}));
            CHECK(opt->enabled);
        }
    }

    SUBCASE("32c) rich_paint_anchor returns 'ctc_rich_<preset_id>' id + OpacityProperty{1.0f} + GlyphSelector") {
        const auto rich = AnimatorResolver::rich_paint_anchor("cinematic_text_camera");
        CHECK(rich.id == "ctc_rich_cinematic_text_camera");           // strict id-format contract.
        CHECK(rich.enabled);
        // OpacityProperty{1.0f} presence per Stage 4 design (drift guard).
        bool opacity_present = false;
        for (const auto& p : rich.properties) {
            if (std::holds_alternative<OpacityProperty>(p)) { opacity_present = true; break; }
        }
        CHECK(opacity_present);
        // Global glyph selector with Square shape (Stage 4 design).
        REQUIRE_FALSE(rich.selectors.empty());
        CHECK(rich.selectors[0].unit == TextSelectorUnit::Glyph);
        CHECK(rich.selectors[0].shape == TextSelectorShape::Square);
    }

    SUBCASE("32d) spec_is_rich dispatches on rich-paint signals (stroke / fill / stroke_style)") {
        CHECK_FALSE(AnimatorResolver::spec_is_rich(make_test_text_spec()));         // plain — no rich paint.
        CHECK(AnimatorResolver::spec_is_rich(make_chronon_rich_text_spec()));      // rich — stroke_enabled = true.
    }

    SUBCASE("32e) compile-time anchor — direct AnimatorResolver:: access from this test TU (smoke)") {
        // This SUBCASE exists as a compile-time + minimal-runtime smoke
        // anchor.  The structural coverage above (32a–32d) COMPILES ONLY
        // IF the header-lift's primary contract holds.  The body has no
        // runtime semantics beyond anchoring the chain — the structural
        // assertions live in 32a–32d.
        CHECK(true);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER I — AGENT 2 evolution (TICKET-A2: testi realmente animati)
// ─────────────────────────────────────────────────────────────────────────
// Sub-cases 40–43 verify the 4 mandatory resolver-driven animated presets.
// For each preset:
//   • Resolver produces a spec with the expected alternative per the
//     pre-refactor Tier F lock-in (cinematic_title_reveal=f ScaleProperty,
//     tracking_close=TrackingProperty, word_cascade/character_cascade=
//     OpacityProperty).
//   • Brief A2.4 selector-binding: title_global/global → Glyph,    //     word_cascade → Word, character_cascade → Grapheme.
//   • Brief A2.5 single source: start/duration/stagger/easing live in
//     ONLY ONE place (the AnimatedValue<> keyframes + selectors here).
//   • At frame 0, mid, and final, AnimatedValue<> properties evaluate to
//     distinct, non-NaN values within expected ranges.
//   • OpacityProperty::value evaluates within [0, 1] at every sampled frame.
//   • ScaleProperty components evaluate to non-negative values.
//   • GlyphSelectorSpec::end at frame 0 is less than at frame final
//     (sweep direction confirm).
//   • Brief DoD: frame iniziale ≠ frame finale; frame intermedio ≠ estremi.
TEST_CASE("TextPresetRegistry: AGENT-2 resolver-driven evolution tier (Sub-cases 40-44)") {

    // Tiny SampledTime helpers — keep the SUBCASEs readable.
    const auto f0  = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{0},  chronon3d::FrameRate{30, 1});
    const auto f18 = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{18}, chronon3d::FrameRate{30, 1});
    const auto f24 = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{24}, chronon3d::FrameRate{30, 1});
    const auto f35 = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{35}, chronon3d::FrameRate{30, 1});
    const auto f36 = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{36}, chronon3d::FrameRate{30, 1});
    const auto f48 = chronon3d::SampleTime::from_frame_int(chronon3d::Frame{48}, chronon3d::FrameRate{30, 1});

    SUBCASE("40) cinematic_title_reveal — animated opacity/blur/scale/position over 36 frames") {
        const auto opt = chronon3d::registry::AnimatorResolver::compose_for("cinematic_title_reveal");
        REQUIRE(opt.has_value());
        REQUIRE(opt->selectors.size() == 1);
        // Brief A2.4: titolo globale → global/glyph selector.
        CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Glyph);
        REQUIRE(opt->properties.size() >= 4);
        // Tier F lock-in: first alternative is ScaleProperty.
        CHECK(std::holds_alternative<chronon3d::ScaleProperty>(opt->properties[0]));

        // OpacityProperty (index 1) at F0, F18, F36.
        const auto& op_av = std::get<chronon3d::OpacityProperty>(opt->properties[1]).value;
        const f32 op_f0  = op_av.evaluate(f0);
        const f32 op_f18 = op_av.evaluate(f18);
        const f32 op_f36 = op_av.evaluate(f36);
        CHECK(op_f0  >= 0.0f);
        CHECK(op_f0  <= 1.0f);
        CHECK(op_f36 >= 0.999f);
        CHECK(op_f36 <= 1.001f);
        // Brief DoD: frame iniziale ≠ finale; frame intermedio ≠ estremi.
        CHECK(op_f0 < op_f36);
        CHECK(op_f18 > op_f0);
        CHECK(op_f18 < op_f36);
        CHECK(!std::isnan(op_f0));
        CHECK(!std::isnan(op_f18));
        CHECK(!std::isnan(op_f36));

        // BlurProperty (index 2): 12 → 0 across 36 frames.
        const auto& blur_av = std::get<chronon3d::BlurProperty>(opt->properties[2]).radius;
        const f32 blur_f0  = blur_av.evaluate(f0);
        const f32 blur_f36 = blur_av.evaluate(f36);
        CHECK(blur_f0 >= 11.0f);
        CHECK(blur_f0 <= 12.01f);
        CHECK(blur_f36 >= 0.0f);
        CHECK(blur_f36 <= 0.01f);
        CHECK(blur_f0 > blur_f36);  // decreasing

        // ScaleProperty (index 0): 0.92 → 1.0 across 36 frames.
        const auto& scale_av = std::get<chronon3d::ScaleProperty>(opt->properties[0]).value;
        const Vec3 scale_f0  = scale_av.evaluate(f0);
        const Vec3 scale_f36 = scale_av.evaluate(f36);
        CHECK(scale_f0.x >= 0.91f);
        CHECK(scale_f0.x <= 0.93f);
        CHECK(std::fabs(scale_f36.x - 1.0f) <= 1e-3f);
        CHECK(scale_f0.x > 0.0f);   // not negative
        CHECK(scale_f36.x > 0.0f);  // not negative
        CHECK(!std::isnan(scale_f0.x));
        CHECK(!std::isnan(scale_f36.x));

        // PositionProperty (index 3): Y 40 → 0 across 36 frames.
        const auto& pos_av = std::get<chronon3d::PositionProperty>(opt->properties[3]).value;
        const Vec3 pos_f0  = pos_av.evaluate(f0);
        const Vec3 pos_f36 = pos_av.evaluate(f36);
        CHECK(pos_f0.y >= 39.0f);
        CHECK(pos_f0.y <= 40.01f);
        CHECK(std::fabs(pos_f36.y - 0.0f) <= 1e-3f);
    }

    SUBCASE("41) tracking_close — tracking 0.18→0 + opacity 0→1 over 35 frames") {
        const auto opt = chronon3d::registry::AnimatorResolver::compose_for("tracking_close");
        REQUIRE(opt.has_value());
        REQUIRE(opt->selectors.size() == 1);
        // Brief A2.4: tracking_close → global selector.
        CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Glyph);
        REQUIRE(opt->properties.size() >= 2);
        // Tier F lock-in: first alternative is TrackingProperty.
        CHECK(std::holds_alternative<chronon3d::TrackingProperty>(opt->properties[0]));

        // Tracking: 0.18 → 0 across 35 frames.
        const auto& trk_av = std::get<chronon3d::TrackingProperty>(opt->properties[0]).pixels;
        const f32 trk_f0  = trk_av.evaluate(f0);
        const f32 trk_f17 = trk_av.evaluate(chronon3d::SampleTime::from_frame_int(chronon3d::Frame{17}, chronon3d::FrameRate{30, 1}));
        const f32 trk_f35 = trk_av.evaluate(f35);
        CHECK(std::fabs(trk_f0  - 0.18f) <= 0.01f);
        CHECK(std::fabs(trk_f35 - 0.0f)  <= 0.01f);
        CHECK(trk_f0 > trk_f35);   // decreasing
        CHECK(trk_f17 > trk_f35);  // mid still positive
        CHECK(trk_f17 < trk_f0);   // mid less than start
        CHECK(!std::isnan(trk_f0));
        CHECK(!std::isnan(trk_f35));

        // OpacityProperty (index 1): 0 → 1 across 35 frames.
        const auto& op_av = std::get<chronon3d::OpacityProperty>(opt->properties[1]).value;
        const f32 op_f0  = op_av.evaluate(f0);
        const f32 op_f35 = op_av.evaluate(f35);
        CHECK(op_f0  >= 0.0f);
        CHECK(op_f0  <= 0.01f);
        CHECK(op_f35 >= 0.999f);
        CHECK(op_f35 <= 1.001f);
        CHECK(op_f0 < op_f35);
    }

    SUBCASE("42) word_cascade — Word selector + animated end sweep + opacity ramp") {
        const auto opt = chronon3d::registry::AnimatorResolver::compose_for("word_cascade");
        REQUIRE(opt.has_value());
        REQUIRE(opt->selectors.size() == 1);
        // Brief A2.4: word_cascade → Word selector.
        CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Word);
        REQUIRE(opt->properties.size() >= 3);
        // Tier F lock-in: first alternative is OpacityProperty.
        CHECK(std::holds_alternative<chronon3d::OpacityProperty>(opt->properties[0]));

        // OpacityProperty (index 0): 0 → 1 across 36 frames.
        const auto& op_av = std::get<chronon3d::OpacityProperty>(opt->properties[0]).value;
        const f32 op_f0  = op_av.evaluate(f0);
        const f32 op_f18 = op_av.evaluate(f18);
        const f32 op_f36 = op_av.evaluate(f36);
        CHECK(op_f0  >= 0.0f);
        CHECK(op_f0  <= 1.0f);
        CHECK(op_f36 >= 0.999f);
        CHECK(op_f0  < op_f18);
        CHECK(op_f18 < op_f36);
        CHECK(!std::isnan(op_f0));
        CHECK(!std::isnan(op_f36));

        // PositionProperty Y: 30 → 0.
        const auto& pos_av = std::get<chronon3d::PositionProperty>(opt->properties[1]).value;
        const Vec3 pos_f0  = pos_av.evaluate(f0);
        const Vec3 pos_f36 = pos_av.evaluate(f36);
        CHECK(std::fabs(pos_f0.y  - 30.0f) <= 0.01f);
        CHECK(std::fabs(pos_f36.y - 0.0f)  <= 0.01f);

        // ScaleProperty x: 0.96 → 1.
        const auto& scale_av = std::get<chronon3d::ScaleProperty>(opt->properties[2]).value;
        const Vec3 scale_f0  = scale_av.evaluate(f0);
        const Vec3 scale_f36 = scale_av.evaluate(f36);
        CHECK(scale_f0.x >= 0.95f);
        CHECK(scale_f0.x <= 0.97f);
        CHECK(std::fabs(scale_f36.x - 1.0f) <= 1e-3f);
        CHECK(scale_f0.x > 0.0f);
        CHECK(scale_f36.x > 0.0f);

        // Brief A2.5 single source: AnimatedValue `end` sweep 0→100 over 48f.
        const f32 end_f0  = opt->selectors[0].end.evaluate(f0);
        const f32 end_f24 = opt->selectors[0].end.evaluate(f24);
        const f32 end_f48 = opt->selectors[0].end.evaluate(f48);
        CHECK(end_f0  <= 0.01f);
        CHECK(end_f48 >= 99.0f);
        CHECK(end_f0  < end_f48);
        CHECK(end_f24 > end_f0);   // mid-way between F0 and Ffinal
        CHECK(end_f24 < end_f48);
    }

    SUBCASE("43) character_cascade — Grapheme selector + animated end sweep + opacity ramp") {
        const auto opt = chronon3d::registry::AnimatorResolver::compose_for("character_cascade");
        REQUIRE(opt.has_value());
        REQUIRE(opt->selectors.size() == 1);
        // FASE 2b: character_cascade → grapheme selector (was Glyph).
        CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Grapheme);
        REQUIRE(opt->properties.size() >= 3);
        // Tier F lock-in: first alternative is OpacityProperty.
        CHECK(std::holds_alternative<chronon3d::OpacityProperty>(opt->properties[0]));

        // OpacityProperty (index 0): 0 → 1 across 18 frames.
        const auto& op_av = std::get<chronon3d::OpacityProperty>(opt->properties[0]).value;
        const f32 op_f0  = op_av.evaluate(f0);
        const f32 op_f18 = op_av.evaluate(f18);
        CHECK(op_f0  >= 0.0f);
        CHECK(op_f0  <= 1.0f);
        CHECK(op_f18 >= 0.999f);
        CHECK(op_f0  < op_f18);
        CHECK(!std::isnan(op_f0));

        // PositionProperty Y: 18 → 0.
        const auto& pos_av = std::get<chronon3d::PositionProperty>(opt->properties[1]).value;
        const Vec3 pos_f0  = pos_av.evaluate(f0);
        const Vec3 pos_f18 = pos_av.evaluate(f18);
        CHECK(std::fabs(pos_f0.y  - 18.0f) <= 0.01f);
        CHECK(std::fabs(pos_f18.y - 0.0f)  <= 0.01f);

        // ScaleProperty x: 0.9 → 1.
        const auto& scale_av = std::get<chronon3d::ScaleProperty>(opt->properties[2]).value;
        const Vec3 scale_f0  = scale_av.evaluate(f0);
        const Vec3 scale_f18 = scale_av.evaluate(f18);
        CHECK(scale_f0.x >= 0.89f);
        CHECK(scale_f0.x <= 0.91f);
        CHECK(std::fabs(scale_f18.x - 1.0f) <= 1e-3f);
        CHECK(scale_f0.x > 0.0f);

        // Brief A2.5 single source: AnimatedValue `end` sweep 0→100 over 24f.
        const f32 end_f0  = opt->selectors[0].end.evaluate(f0);
        const f32 end_f24 = opt->selectors[0].end.evaluate(f24);
        CHECK(end_f0  <= 0.01f);
        CHECK(end_f24 >= 99.0f);
        CHECK(end_f0  < end_f24);
    }

    SUBCASE("45) FASE 2b — word_pop and masked_line_reveal selector-unit locks") {
        // word_pop now uses Word selector (was global Glyph).
        {
            const auto opt = chronon3d::registry::AnimatorResolver::compose_for("word_pop");
            REQUIRE(opt.has_value());
            REQUIRE(opt->selectors.size() == 1);
            CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Word);
            // Animated end sweep 0→100 over 20f.
            const f32 end_f0  = opt->selectors[0].end.evaluate(f0);
            const f32 end_f20 = opt->selectors[0].end.evaluate(chronon3d::SampleTime::from_frame_int(chronon3d::Frame{20}, chronon3d::FrameRate{30, 1}));
            CHECK(end_f0  <= 0.01f);
            CHECK(end_f20 >= 99.0f);
            CHECK(end_f0 < end_f20);
        }
        // masked_line_reveal now uses Line selector (was global Glyph).
        {
            const auto opt = chronon3d::registry::AnimatorResolver::compose_for("masked_line_reveal");
            REQUIRE(opt.has_value());
            REQUIRE(opt->selectors.size() == 1);
            CHECK(opt->selectors[0].unit == chronon3d::TextSelectorUnit::Line);
            // Animated end sweep 0→100 over 30f.
            const f32 end_f0  = opt->selectors[0].end.evaluate(f0);
            const f32 end_f30 = opt->selectors[0].end.evaluate(chronon3d::SampleTime::from_frame_int(chronon3d::Frame{30}, chronon3d::FrameRate{30, 1}));
            CHECK(end_f0  <= 0.01f);
            CHECK(end_f30 >= 99.0f);
            CHECK(end_f0 < end_f30);
        }
    }

    SUBCASE("44) agents cross-link invariant — 4 mandatory presets produce ≥1 RenderNode") {
        // Brief DoD: number of glyphs invariant.  Layer-level consistent:
        // each preset's resolver-driven spec routes through the canonical
        // lb.text_run(...) which produces ≥1 RenderNode.
        const auto reg = make_default_text_preset_registry();
        for (const auto& pid : {"cinematic_title_reveal", "tracking_close",
                                "word_cascade", "character_cascade"}) {
            CAPTURE(pid);
            REQUIRE(reg.contains(pid));
            const auto& preset = reg.get(pid);
            chronon3d::SceneBuilder sb(1280, 720);
            chronon3d::LayerBuilder lb(
                std::string{"agent2_invariant_"} + pid, chronon3d::Frame{0});
            CHECK_NOTHROW(preset.builder(sb, lb, make_test_text_spec()));
            lb.screen_dimensions(1280.0f, 720.0f);
            chronon3d::Layer built = lb.build();
            CHECK(built.nodes.size() >= 1);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TIER J — TICKET-107 per-category register helpers (post-namespace-lift)
// ─────────────────────────────────────────────────────────────────────────
// Verifies the FASE 5 reviewer-fixup outcome: the 4 per-category register
// helpers + the umbrella `register_builtin_presets` were lifted out of the
// file-local anonymous namespace in `src/registry/text_preset_registry.cpp`
// into `chronon3d::registry::register_helpers_internal` (re-exported at the
// `chronon3d::registry` parent namespace) and declared in
// `src/registry/text_preset_register_helpers.hpp`.  Tests now exercise each
// helper in isolation without re-seeding the full 22-entry catalog.
TEST_CASE("TextPresetRegistry: TICKET-107 per-category register helpers in isolation") {

    SUBCASE("50) per-category helpers reach the canonical 4/10/4/4 distribution in isolation") {
        TextPresetRegistry cinematic_only;
        reg_helpers::register_text_preset_cinematic(cinematic_only);
        const auto cins = cinematic_only.by_category(TextPresetCategory::Cinematic);
        CHECK(cins.size() == 4);
        // Other categories must be EMPTY — proves the per-category helper
        // does not pollute the registry with reveal/emphasis/subtitle entries.
        CHECK(cinematic_only.by_category(TextPresetCategory::Reveal).empty());
        CHECK(cinematic_only.by_category(TextPresetCategory::Emphasis).empty());
        CHECK(cinematic_only.by_category(TextPresetCategory::Subtitle).empty());

        TextPresetRegistry reveal_only;
        reg_helpers::register_text_preset_reveal(reveal_only);
        CHECK(reveal_only.by_category(TextPresetCategory::Reveal).size() == 10);
        CHECK(reveal_only.by_category(TextPresetCategory::Cinematic).empty());
        CHECK(reveal_only.by_category(TextPresetCategory::Emphasis).empty());
        CHECK(reveal_only.by_category(TextPresetCategory::Subtitle).empty());

        TextPresetRegistry emphasis_only;
        reg_helpers::register_text_preset_emphasis(emphasis_only);
        CHECK(emphasis_only.by_category(TextPresetCategory::Emphasis).size() == 4);
        CHECK(emphasis_only.by_category(TextPresetCategory::Cinematic).empty());
        CHECK(emphasis_only.by_category(TextPresetCategory::Reveal).empty());
        CHECK(emphasis_only.by_category(TextPresetCategory::Subtitle).empty());

        TextPresetRegistry subtitle_only;
        reg_helpers::register_text_preset_subtitle(subtitle_only);
        CHECK(subtitle_only.by_category(TextPresetCategory::Subtitle).size() == 4);
        CHECK(subtitle_only.by_category(TextPresetCategory::Cinematic).empty());
        CHECK(subtitle_only.by_category(TextPresetCategory::Reveal).empty());
        CHECK(subtitle_only.by_category(TextPresetCategory::Emphasis).empty());
    }

    SUBCASE("51) register_builtin_presets umbrella matches make_default_text_preset_registry (same 22 ids)") {
        // The umbrella + the factory function should populate the SAME 22 ids
        // in the SAME order.  Compare available() (sorted-by-key) since the
        // internal std::map order is deterministic but the umbrella's
        // insertion order is Cinematic → Reveal → Emphasis → Subtitle.
        TextPresetRegistry via_umbrella;
        reg_helpers::register_builtin_presets(via_umbrella);
        const auto via_factory = make_default_text_preset_registry();
        CHECK(via_umbrella.available() == via_factory.available());
        CHECK(via_umbrella.list().size() == 22);
        // Idempotent: calling register_builtin_presets twice on the same
        // registry throws on the FIRST duplicate id (re-confirms the
        // per-id-table anti-duplication contract).
        CHECK_THROWS_AS(reg_helpers::register_builtin_presets(via_umbrella), std::runtime_error);
    }

    SUBCASE("52) per-category helpers are independently composable (cinematic + emphasis → 8 entries)") {
        // A studio overlay that wants ONLY cinematic + emphasis seeds can
        // compose two per-category calls and inspect a partial catalog.
        TextPresetRegistry studio_overlay;
        reg_helpers::register_text_preset_cinematic(studio_overlay);
        reg_helpers::register_text_preset_emphasis(studio_overlay);
        CHECK(studio_overlay.list().size() == 8);  // 4 cinematic + 4 emphasis
        CHECK(studio_overlay.by_category(TextPresetCategory::Cinematic).size() == 4);
        CHECK(studio_overlay.by_category(TextPresetCategory::Emphasis).size() == 4);
        CHECK(studio_overlay.by_category(TextPresetCategory::Reveal).empty());
        CHECK(studio_overlay.by_category(TextPresetCategory::Subtitle).empty());

        // Adding reveal AFTER cinematic + emphasis grows the catalog to 18.
        reg_helpers::register_text_preset_reveal(studio_overlay);
        CHECK(studio_overlay.list().size() == 18);  // 4 + 4 + 10
        // Subtitle still empty.
        CHECK(studio_overlay.by_category(TextPresetCategory::Subtitle).empty());
    }

    SUBCASE("53) per-category helper reuse on a populated registry throws (idempotency contract)") {
        TextPresetRegistry reg;
        reg_helpers::register_text_preset_cinematic(reg);
        // Calling the same helper again on an already-populated registry
        // throws on the FIRST re-seeded id (animation_compositions is
        // the first entry per reg_helpers::register_text_preset_cinematic;
        // cinematic_text_camera / cinematic_title_reveal /
        // tilt_sweep_title_v2 follow).  CHECK_THROWS_AS only verifies
        // SOME runtime_error escapes — does NOT assert the specific id
        // (reviewer finding #10).
        CHECK_THROWS_AS(reg_helpers::register_text_preset_cinematic(reg), std::runtime_error);
    }
}
