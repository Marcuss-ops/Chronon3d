// ==============================================================================
// tests/golden/test_clip_transition_golden.cpp
//
// Golden regression suite for ClipTransitionNode Cut/Dissolve at exact
// progress 0/25/50/75/100 %.  Each case renders a 256x256 composition where
// layer A is solid red and layer B is solid blue, with a 28-frame clip
// transition so the sampled frames align exactly with the labelled progress.
//
// Golden PNGs:   test_renders/golden/clip_transition/
// Artifacts:     test_renders/artifacts/clip_transition/
// Update:        CHRONON3D_UPDATE_GOLDENS=1 ctest -R ClipTransitionGolden
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

constexpr int kClipTransitionDuration = 28;

Composition make_clip_transition_comp(ClipTransitionKind kind) {
    return composition({.width = 256, .height = 256, .duration = kClipTransitionDuration + 1},
        [kind](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);

            s.layer("a", [](LayerBuilder& l) {
                l.rect("red", {.size = {256.0f, 256.0f}, .color = Color::red()});
            });

            s.layer("b", [](LayerBuilder& l) {
                l.rect("blue", {.size = {256.0f, 256.0f}, .color = Color::blue()});
            });

            ClipTransitionSpec spec;
            spec.kind = kind;
            spec.easing = Easing::Linear;
            s.clip_transition("a", "b", spec, Frame{0}, Frame{kClipTransitionDuration});

            return s.build();
        });
}

GoldenTestConfig clip_transition_golden_config() {
    return {
        .golden_directory   = "test_renders/golden/clip_transition",
        .artifact_directory = "test_renders/artifacts/clip_transition",
        .threshold          = {},
        .mode               = golden_mode_from_environment(),
    };
}

void verify_clip_transition_golden(SoftwareRenderer& renderer,
                                     ClipTransitionKind kind,
                                     int frame,
                                     const std::string& label) {
    auto comp = make_clip_transition_comp(kind);
    auto fb = renderer.render(comp, Frame{frame});
    REQUIRE(fb != nullptr);

    const auto result = verify_golden(*fb, label, clip_transition_golden_config());
    INFO(result.message);
    CHECK(result.passed);
}

} // namespace

TEST_CASE("ClipTransitionGolden: Cut at 0/25/50/75/100 %") {
    auto renderer = test::make_renderer();
    verify_clip_transition_golden(renderer, ClipTransitionKind::Cut, 0,  "clip_transition_cut_p000");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Cut, 7,  "clip_transition_cut_p025");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Cut, 14, "clip_transition_cut_p050");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Cut, 21, "clip_transition_cut_p075");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Cut, 28, "clip_transition_cut_p100");
}

TEST_CASE("ClipTransitionGolden: Dissolve at 0/25/50/75/100 %") {
    auto renderer = test::make_renderer();
    verify_clip_transition_golden(renderer, ClipTransitionKind::Dissolve, 0,  "clip_transition_dissolve_p000");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Dissolve, 7,  "clip_transition_dissolve_p025");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Dissolve, 14, "clip_transition_dissolve_p050");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Dissolve, 21, "clip_transition_dissolve_p075");
    verify_clip_transition_golden(renderer, ClipTransitionKind::Dissolve, 28, "clip_transition_dissolve_p100");

    // Robustness guard: at 50% the center pixel should be the blend of
    // red and blue (magenta), confirming the dissolve is mathematically
    // correct before relying solely on the golden comparison.
    {
        auto comp = make_clip_transition_comp(ClipTransitionKind::Dissolve);
        auto fb = renderer.render(comp, Frame{14});
        REQUIRE(fb != nullptr);
        const Color c = fb->get_pixel(128, 128);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(c.g == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(c.b == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(c.a == doctest::Approx(1.0f).epsilon(0.01f));
    }
}
