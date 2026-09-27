#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

using namespace chronon3d;

namespace {
std::shared_ptr<Framebuffer> render_glass_backdrop(BackdropBlurParams params) {
    auto renderer = test::make_renderer();
    RenderSettings settings;
    renderer.set_settings(settings);
    auto comp = composition({.width = 128, .height = 128, .duration = 1},
        [params](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            for (int i = 0; i < 16; ++i) {
                const float t = static_cast<float>(i) / 15.0f;
                scene.layer("stripe_" + std::to_string(i), [i, t](LayerBuilder& layer) {
                    const float x = -60.0f + static_cast<float>(i) * 8.0f;
                    layer.rect("stripe", RectParams{
                        .size = {8.0f, 128.0f},
                        .color = {0.1f + 0.8f * t, 0.15f + 0.5f * (1.0f - t), 0.8f - 0.6f * t, 1.0f},
                        .pos = {x, 0.0f, 0.0f}});
                });
            }
            scene.layer("glass", [params](LayerBuilder& layer) {
                layer.backdrop_blur(params);
                layer.rounded_rect("card", RoundedRectParams{
                    .size = {104.0f, 92.0f}, .radius = 18.0f,
                    .color = {1.0f, 1.0f, 1.0f, 0.16f}});
            });
            return scene.build();
        });
    return renderer.render(comp, 0);
}
}

TEST_CASE("Glass backdrop refraction, grain, and directional rim change card pixels") {
    const auto plain = render_glass_backdrop(BackdropBlurParams{.blur = 8.0f});
    const auto glass = render_glass_backdrop(BackdropBlurParams{
        .blur = 8.0f, .rim_light = 0.8f, .refraction = 2.5f,
        .surface_noise = 0.035f});
    REQUIRE(plain);
    REQUIRE(glass);
    float max_delta = 0.0f;
    for (int y = 12; y < 116; ++y) for (int x = 12; x < 116; ++x) {
        const Color a = plain->get_pixel(x, y);
        const Color b = glass->get_pixel(x, y);
        max_delta = std::max(max_delta, std::abs(a.r - b.r) +
            std::abs(a.g - b.g) + std::abs(a.b - b.b));
    }
    CHECK(max_delta > 0.02f);
    CHECK(glass->get_pixel(64, 64).a == doctest::Approx(plain->get_pixel(64, 64).a));
}

TEST_CASE("Composite cache identity includes glass controls") {
    graph::RenderGraphContext context;
    context.frame_input = test::make_render_frame_info(128, 128);
    const BackdropBlurParams baseline{
        .blur = 8.0f, .rim_light = 0.2f, .refraction = 1.0f,
        .surface_noise = 0.01f};
    graph::CompositeNode original{9001, BlendMode::Normal};
    original.set_backdrop_blur(baseline);
    const auto original_key = original.cache_key(context);

    for (int control = 0; control < 3; ++control) {
        auto changed = baseline;
        if (control == 0) changed.rim_light += 0.1f;
        if (control == 1) changed.refraction += 0.1f;
        if (control == 2) changed.surface_noise += 0.01f;
        graph::CompositeNode node{9001, BlendMode::Normal};
        node.set_backdrop_blur(changed);
        CHECK(node.cache_key(context).params_hash != original_key.params_hash);
    }
}
