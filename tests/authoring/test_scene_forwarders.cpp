// Authoring Scene facade tests. Only the canonical authoring surfaces are
// certified here; raw SceneBuilder/LayerBuilder passthrough behaviour belongs
// to the lower-level builder suites.

#include <doctest/doctest.h>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/camera_api.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <string_view>

namespace {

chronon3d::FrameContext engine_ctx(chronon3d::Frame frame = chronon3d::Frame{0}) {
    chronon3d::FrameContext context;
    context.frame = frame;
    context.frame_rate = chronon3d::FrameRate{30, 1};
    context.width = 1920;
    context.height = 1080;
    return context;
}

chronon3d::CanvasInfo author_canvas(float width = 1920.0f,
                                    float height = 1080.0f) {
    return chronon3d::CanvasInfo::with_safe_area(
        width, height, chronon3d::SafeAreaPreset{});
}

[[nodiscard]] bool scene_has_node(const chronon3d::Scene& scene,
                                  std::string_view name) {
    for (const auto& node : scene.nodes()) {
        if (node.name == name) return true;
    }
    return false;
}

[[nodiscard]] bool scene_has_layer(const chronon3d::Scene& scene,
                                   std::string_view name) {
    for (const auto& layer : scene.layers()) {
        if (layer.name == name) return true;
    }
    return false;
}

} // namespace

TEST_CASE("Authoring / Scene::camera returns the canonical CameraApi") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{200})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    static_assert(std::is_class_v<chronon3d::CameraApi>);
    scene.camera()
         .position(chronon3d::Vec3{10.0f, 20.0f, 30.0f})
         .zoom(2.0f);

    const chronon3d::Camera2_5D& camera = builder.camera_2_5d();
    CHECK(camera.zoom == doctest::Approx(2.0f));
    CHECK(camera.position.x != 0.0f);
}

TEST_CASE("Authoring / Scene::background chains with canonical Layer facade") {
    chronon3d::SceneBuilder builder{engine_ctx()};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    chronon3d::authoring::Scene& result = scene.background(
        "stage_bg",
        chronon3d::GridBackgroundParams{
            .size = {1920.0f, 1080.0f},
            .bg_color = chronon3d::Color{0.05f, 0.05f, 0.05f, 1.0f},
            .spacing = 80.0f,
        });
    CHECK(&result == &scene);

    scene.layer("title", [](chronon3d::authoring::Layer& layer) {
        (void)layer.rect(
            "r",
            {.size = {100.0f, 50.0f}, .color = chronon3d::Color::white()});
    });

    chronon3d::Scene evaluated = builder.build();
    CHECK(scene_has_node(evaluated, "stage_bg"));
    CHECK(scene_has_layer(evaluated, "title"));
}

TEST_CASE("Authoring / Scene::image adds the named image layer") {
    chronon3d::SceneBuilder builder{engine_ctx()};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    chronon3d::authoring::Scene& result = scene.image(
        "logo",
        chronon3d::ImageParams{
            .asset_path = "images/logo.png",
            .size = {200.0f, 200.0f},
            .pos = {960.0f, 540.0f, 0.0f},
        });
    CHECK(&result == &scene);

    chronon3d::Scene evaluated = builder.build();
    CHECK(scene_has_layer(evaluated, "logo"));
}

TEST_CASE("Authoring / Scene::screen_layer wraps the canonical Layer facade") {
    chronon3d::SceneBuilder builder{engine_ctx()};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    bool invoked = false;
    scene.screen_layer("overlay", [&](chronon3d::authoring::Layer& layer) {
        invoked = true;
        (void)layer.rect(
            "r",
            {.size = {50.0f, 50.0f}, .color = chronon3d::Color::white()});
    });

    CHECK(invoked);
    chronon3d::Scene evaluated = builder.build();
    CHECK(scene_has_layer(evaluated, "overlay"));
}

TEST_CASE("Authoring / Scene::precomp wraps the canonical Layer facade") {
    chronon3d::SceneBuilder builder{engine_ctx()};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    bool invoked = false;
    scene.precomp(
        "main_precomp",
        "intro_clip",
        [&](chronon3d::authoring::Layer& layer) {
            invoked = true;
            (void)layer.rect(
                "r",
                {.size = {30.0f, 30.0f}, .color = chronon3d::Color::white()});
        });

    CHECK(invoked);
    chronon3d::Scene evaluated = builder.build();
    CHECK(scene_has_layer(evaluated, "main_precomp"));
}

TEST_CASE("Authoring / Scene canonical verbs compose in one fluent chain") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{30})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    scene.background("bg", chronon3d::GridBackgroundParams{})
         .image("logo",
                chronon3d::ImageParams{
                    .asset_path = "logo.png",
                    .size = {200.0f, 200.0f}
                })
         .sequence(
             "intro",
             {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{60}},
             [](chronon3d::SequenceBuilder& sequence) {
                 sequence.layer("title", [](chronon3d::LayerBuilder& layer) {
                     layer.rect(
                         "r",
                         {.size = {100.0f, 50.0f},
                          .color = chronon3d::Color::white()});
                 });
             })
         .screen_layer("hud", [](chronon3d::authoring::Layer& layer) {
             (void)layer.rect(
                 "r",
                 {.size = {10.0f, 10.0f},
                  .color = chronon3d::Color::white()});
         });

    scene.camera().zoom(1.5f);

    chronon3d::Scene evaluated = builder.build();
    CHECK(scene_has_node(evaluated, "bg"));
    CHECK(scene_has_layer(evaluated, "logo"));
    CHECK(scene_has_layer(evaluated, "hud"));
    CHECK(scene_has_layer(evaluated, "title"));
    CHECK(evaluated.camera_2_5d().zoom == doctest::Approx(1.5f));
}
