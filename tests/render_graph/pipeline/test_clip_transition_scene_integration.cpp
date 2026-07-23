// ==============================================================================
// tests/render_graph/pipeline/test_clip_transition_scene_integration.cpp
//
// TRN-07 — SceneBuilder clip_transition integration.
// Verifies that ClipTransitionNode is wired into the render graph when a
// scene is built with SceneBuilder::clip_transition().
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;

namespace {

Composition make_clip_transition_comp(ClipTransitionKind kind) {
    return composition({.width = 256, .height = 256, .duration = 60},
        [kind](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("a", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rect("red", {
                    .size = {256.0f, 256.0f},
                    .color = Color::red(),
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });

            s.layer("b", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.rect("blue", {
                    .size = {256.0f, 256.0f},
                    .color = Color::blue(),
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });

            ClipTransitionSpec spec;
            spec.kind = kind;
            spec.easing = Easing::Linear;
            s.clip_transition("a", "b", spec, Frame{0}, Frame{30});

            return s.build();
        });
}

Color sample_center(const std::shared_ptr<Framebuffer>& fb) {
    REQUIRE(fb != nullptr);
    return fb->get_pixel(fb->width() / 2, fb->height() / 2);
}

} // namespace

TEST_CASE("SceneBuilder clip_transition Cut returns A before and B after") {
    auto renderer = test::make_renderer_shared();
    auto comp = make_clip_transition_comp(ClipTransitionKind::Cut);

    auto fb_before = renderer->render(comp, 0);
    auto center_before = sample_center(fb_before);
    CHECK(center_before.r == doctest::Approx(1.0f));
    CHECK(center_before.g == doctest::Approx(0.0f));
    CHECK(center_before.b == doctest::Approx(0.0f));

    auto fb_after = renderer->render(comp, 30);
    auto center_after = sample_center(fb_after);
    CHECK(center_after.r == doctest::Approx(0.0f));
    CHECK(center_after.g == doctest::Approx(0.0f));
    CHECK(center_after.b == doctest::Approx(1.0f));
}

TEST_CASE("SceneBuilder clip_transition Dissolve blends A and B at midpoint") {
    auto renderer = test::make_renderer_shared();
    auto comp = make_clip_transition_comp(ClipTransitionKind::Dissolve);

    auto fb_mid = renderer->render(comp, 15);
    auto center = sample_center(fb_mid);
    CHECK(center.r == doctest::Approx(0.5f).epsilon(0.02f));
    CHECK(center.g == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(center.b == doctest::Approx(0.5f).epsilon(0.02f));
    CHECK(center.a == doctest::Approx(1.0f).epsilon(0.01f));
}
