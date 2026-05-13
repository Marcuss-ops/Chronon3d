#include <chronon3d/chronon3d.hpp>
#include <cmath>
#include <string>

using namespace chronon3d;

static Composition ParentingRigProof() {
    return composition({
        .name = "ParentingRigProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.03f, 0.04f, 0.09f, 1},
            .pos = {0, 0, 0}
        });

        s.null_layer("card_rig", [&](LayerBuilder& l) {
            l.position({
                keyframes<f32>({
                    {0, -250.0f, Easing::OutCubic},
                    {60, 0.0f, Easing::OutBack},
                    {119, 220.0f, Easing::InOutCubic}
                }).value(ctx.frame),
                std::sin(static_cast<f32>(ctx.frame) * 0.05f) * 30.0f,
                0
            })
            .rotate({0, 0, std::sin(static_cast<f32>(ctx.frame) * 0.04f) * 4.0f});
        });

        s.layer("card", [](LayerBuilder& l) {
            l.parent("card_rig")
             .enable_3d()
             .depth_role(DepthRole::Subject);

            l.rounded_rect("body", {
                .size = {520, 300},
                .radius = 36,
                .color = Color{0.12f, 0.20f, 0.55f, 1},
                .pos = {0, 0, 0}
            });

            TextStyle title{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 42.0f,
                .color = Color::white()
            };

            l.text("title", {
                .content = "PARENTED CARD",
                .style = title,
                .pos = {-190, -20, 0}
            });
        });

        s.layer("satellite_left", [](LayerBuilder& l) {
            l.parent("card_rig")
             .enable_3d()
             .depth_role(DepthRole::Foreground)
             .opacity(0.8f);

            l.circle("dot", {
                .radius = 30,
                .color = Color{0.4f, 0.8f, 1.0f, 1},
                .pos = {-330, -190, 0}
            });
        });

        s.layer("satellite_right", [](LayerBuilder& l) {
            l.parent("card_rig")
             .enable_3d()
             .depth_role(DepthRole::Foreground)
             .opacity(0.8f);

            l.circle("dot", {
                .radius = 30,
                .color = Color{1.0f, 0.4f, 0.7f, 1},
                .pos = {330, 190, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ParentingRigProof", ParentingRigProof)
