// ─── test_text_preset_descriptor.cpp — TEXT-RES-01 dedicated test ───────────
//
// Verifies the SINGLE-REGISTRY invariant from `TEXT-RES-01`:
//   1. Each of the 22 built-in text presets is registered via
//      `TextPresetDescriptor` with shape
//      `{ id, metadata, builder, animator_factory, fixture }`.
//   2. Every `animator_factory` closure is NON-NULL for the 21 motion
//      presets + returns std::nullopt for `minimal_white` (the only
//      preset with no canonical motion).
//   3. Every `fixture` (golden-frame fixture path) is NON-EMPTY for
//      all 22 entries — the Visual Regression Harness link is locked
//      at registration time, NOT in a sidecar comment.
//   4. `id` and `metadata.id` are kept in sync at the descriptor
//      level (register-time invariant).
//   5. The animator_factory contract: `register_builtin_presets()`
//      and `AnimatorResolver::compose_for(<id>)` agree on the
//      output.  AnimatorResolver is a thin registry dispatcher —
//      it MUST NOT maintain a parallel per-id table.
//
// Mirrors tests/test_text_preset_registry.cpp Tier-A style: pure
// registry inspection + Spot-composition probes; no LayerBuilder
// / SceneBuilder / FontEngine instantiation required.
//
// Build wiring: this source is added to `chronon3d_core_tests`
// in `tests/core_tests.cmake` alongside `test_text_preset_registry.cpp`.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/text_preset_registry.hpp>
#include <chronon3d/registry/text_preset_descriptor.hpp>
#include <chronon3d/registry/animator_resolver.hpp>

#include <string>
#include <vector>
#include <variant>

using chronon3d::registry::TextPresetDescriptor;
using chronon3d::registry::builtin_text_preset_registry;
using chronon3d::registry::make_default_text_preset_registry;
using chronon3d::registry::AnimatorResolver;

// ─────────────────────────────────────────────────────────────────────────
// TIER A — descriptor field completeness (all 22 built-ins)
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("TextPresetDescriptor: each built-in populates {id, metadata, builder, animator_factory, fixture} (Sub-cases A1-A4)") {

    SUBCASE("A1) every built-in id and metadata.id are non-empty AND in sync at registration time") {
        const auto& r = builtin_text_preset_registry();
        REQUIRE(r.list().size() >= 21);  // DoD #1 floor — matches Sub-case 2 of test_text_preset_registry.cpp.

        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK_FALSE(d.id.empty());
            CHECK_FALSE(d.metadata.id.empty());
            // TEXT-RES-01 invariant: top-level `id` == `metadata.id`
            // (kept in sync by `TextPresetRegistry::register_preset`).
            CHECK(d.id == d.metadata.id);
        }
    }

    SUBCASE("A2) every built-in has a non-null builder + animator_factory closure") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK(static_cast<bool>(d.builder));         // engine-side factory must be wired.
            CHECK(static_cast<bool>(d.animator_factory)); // resolver-side factory must be wired.
        }
    }

    SUBCASE("A3) every built-in `fixture` field (golden-frame path) is non-empty") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK_FALSE(d.fixture.empty());
            // Convention: fixture paths are rooted under `tests/visual/`.
            CHECK(d.fixture.rfind("tests/visual/", 0) == 0);
        }
    }

    SUBCASE("A4) every built-in has builtin=true + non-empty metadata.display_name + .description") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            CHECK(d.metadata.builtin == true);
            CHECK_FALSE(d.metadata.display_name.empty());
            CHECK_FALSE(d.metadata.description.empty());
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER B — single-source-of-truth invariant (the TEXT-RES-01 contract)
// ─────────────────────────────────────────────────────────────────────────
//
// Verifies that AnimatorResolver is a thin dispatcher INTO the registry's
// per-entry `animator_factory` closures — NOT a parallel per-id table.
//
// Two complementary checks per preset:
//   (1) PROBE registr: ask `r.get(<id>).animator_factory(metadata)` and
//       inspect the resulting optional<TextAnimatorSpec>.
//   (2) PROBE resolver: ask `AnimatorResolver::compose_for(<id>)` and
//       inspect the resulting optional<TextAnimatorSpec>.
//
// Both probes MUST produce equivalent specs (id + first property kind).
// If they diverge, AnimatorResolver has reverted to maintaining its own
// per-id table — a regression on the TEXT-RES-01 single-source rule.
TEST_CASE("TextPresetDescriptor: AnimatorResolver queries registry exactly (Sub-cases B1-B2)") {

    SUBCASE("B1) compose_for(<id>) returns std::nullopt iff descriptor.animator_factory returns std::nullopt") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            const auto from_factory = d.animator_factory
                ? d.animator_factory(d.metadata)
                : std::nullopt;
            const auto from_resolver = AnimatorResolver::compose_for(d.id);
            CHECK(from_factory.has_value() == from_resolver.has_value());
        }
    }

    SUBCASE("B2) compose_for(<id>) returns a spec whose id matches presetc_<id> (when non-null)") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& d : r.list()) {
            CAPTURE(d.id);
            const auto opt = AnimatorResolver::compose_for(d.id);
            if (!opt.has_value()) continue;  // minimal_white (no motion).
            CHECK(opt->id == (std::string{"presetc_"} + d.id));
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER C — Spot-checks: first-property-kind mapping (drift catch)
// ─────────────────────────────────────────────────────────────────────────
//
// Lock-in: for each preset_id the registry's animator_factory closure
// pushes the same FIRST property alternative as the pre-TEXT-RES-01
// resolver branch.  Catches future drifts where someone refactors
// compose_<id>() and accidentally swaps the first property kind.
TEST_CASE("TextPresetDescriptor: first-property-kind mapping for representative presets (Sub-case C1)") {

    using P = chronon3d::PositionProperty;
    using S = chronon3d::ScaleProperty;
    using O = chronon3d::OpacityProperty;
    using B = chronon3d::BlurProperty;
    using T = chronon3d::TrackingProperty;

    struct Expected { std::string id; std::function<bool(const chronon3d::TextAnimatorProperty&)> kind; };
    const std::vector<Expected> expectations = {
        {"animation_compositions",  [](const auto& p){ return std::holds_alternative<P>(p); }},
        {"cinematic_text_camera",   [](const auto& p){ return std::holds_alternative<P>(p); }},
        {"cinematic_title_reveal",  [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"tilt_sweep_title_v2",     [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"text_animations",         [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"fade_in",                 [](const auto& p){ return std::holds_alternative<O>(p); }},
        {"blur_in",                 [](const auto& p){ return std::holds_alternative<B>(p); }},
        {"slide_up",                [](const auto& p){ return std::holds_alternative<P>(p); }},
        {"slide_down",              [](const auto& p){ return std::holds_alternative<P>(p); }},
        {"scale_in",                [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"tracking_close",          [](const auto& p){ return std::holds_alternative<T>(p); }},
        {"masked_line_reveal",      [](const auto& p){ return std::holds_alternative<P>(p); }},
        {"word_cascade",            [](const auto& p){ return std::holds_alternative<O>(p); }},
        {"character_cascade",       [](const auto& p){ return std::holds_alternative<O>(p); }},
        {"word_pop",                [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"scale_punch",             [](const auto& p){ return std::holds_alternative<S>(p); }},
        {"color_accent",            [](const auto& p){ return std::holds_alternative<O>(p); }},
        {"gradient_fill",           [](const auto& p){ return std::holds_alternative<P>(p); }},
        // minimal_white: no resolver output, predicate unused.
        {"yellow_keyword",          [](const auto& p){ return std::holds_alternative<O>(p); }},
        {"glow_pulse",              [](const auto& p){ return std::holds_alternative<T>(p); }},
        {"caption_box",             [](const auto& p){ return std::holds_alternative<P>(p); }},
    };

    SUBCASE("C1) for the 21 motion presets, compose_for(<id>).properties[0] matches the expected first-kind") {
        const auto& r = builtin_text_preset_registry();
        for (const auto& exp : expectations) {
            CAPTURE(exp.id);
            REQUIRE(r.contains(exp.id));
            const auto opt = AnimatorResolver::compose_for(exp.id);
            REQUIRE(opt.has_value());  // 21/22 — minimal_white already excluded.
            REQUIRE_FALSE(opt->properties.empty());
            CHECK(exp.kind(opt->properties[0]));
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────
// TIER D — fail-safe paths (Sub-cases D1-D2)
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("TextPresetDescriptor: fail-safe paths (Sub-cases D1-D2)") {

    SUBCASE("D1) AnimatorResolver::compose_for returns std::nullopt for unknown id") {
        CHECK_FALSE(AnimatorResolver::compose_for("phantom_unknown_id").has_value());
        CHECK_FALSE(AnimatorResolver::compose_for("").has_value());
    }

    SUBCASE("D2) builtin_text_preset_registry returns a frozen registry") {
        auto r = builtin_text_preset_registry();
        CHECK(r.is_frozen());
        // Subsequent register attempts throw.
        TextPresetDescriptor rogue;
        rogue.id = "rogue_addition";
        rogue.metadata.id = rogue.id;
        rogue.builder = [](chronon3d::SceneBuilder&, chronon3d::LayerBuilder&,
                          const chronon3d::TextSpec&){};
        CHECK_THROWS_AS(r.register_preset(rogue), std::runtime_error);
    }
}
