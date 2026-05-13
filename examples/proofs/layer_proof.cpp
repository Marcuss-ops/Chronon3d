#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <chronon3d/scene/layer_builder.hpp>

namespace chronon3d {

Composition LayerProof() {
    return composition({
        .name = "LayerProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        TextStyle title_style{
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .size = 60.0f,
            .color = Color{1, 1, 1, 1}
        };

        TextStyle sub_style{
            .font_path = "assets/fonts/Inter-Regular.ttf",
            .size = 28.0f,
            .color = Color{0.75f, 0.82f, 1.0f, 1}
        };

        s.rect("background", {
            .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {640, 360, 0}
        });

        s.layer("card-layer", [&](LayerBuilder& l) {
            l.position({640, 360, 0})
             .opacity(1.0f);

            l.rounded_rect("card-bg", {
                .size = {760, 260},
                .radius = 32.0f,
                .color = Color{0.10f, 0.12f, 0.22f, 1},
                .pos = {0, 0, 0}
            }).with_shadow({
                .enabled = true,
                .offset = {0, 12},
                .color = {0, 0, 0, 0.45f},
                .radius = 24
            });

            l.text("title", {
                .content = "LAYER SYSTEM",
                .style = title_style,
                .pos = {-300, -55, 0}
            });

            l.text("subtitle", {
                .content = "grouped transform hierarchy",
                .style = sub_style,
                .pos = {-300, 35, 0}
            });

            l.line("underline", {
                .from = {-300, 90, 0},
                .to = {300, 90, 0},
                .thickness = 1.0f,
                .color = Color{0.35f, 0.65f, 1.0f, 1}
            });
        });

        s.layer("badge-layer", [&](LayerBuilder& l) {
            l.position({900, 250, 0});
            l.circle("badge", {
                .radius = 34.0f,
                .color = Color{1.0f, 0.72f, 0.18f, 1},
                .pos = {0, 0, 0}
            }).with_glow({
                .enabled = true,
                .radius = 16.0f,
                .intensity = 0.5f,
                .color = {1.0f, 0.72f, 0.18f, 0.5f}
            });
        });

        return s.build();
    });
}

Composition AnimatedLayerProof() {
    return composition({
        .name = "AnimatedLayerProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("background", {
            .size = {1280, 720},
            .color = Color{0.015f, 0.015f, 0.025f, 1},
            .pos = {640, 360, 0}
        });

        auto intro = sequence(ctx, 0, 70);
        auto y = spring(intro, -180.0f, 360.0f);
        auto alpha = interpolate(ctx.frame, 0, 25, 0.0f, 1.0f);
        auto scale_value = interpolate(ctx.frame, 0, 45, 0.85f, 1.0f, Easing::OutCubic);

        TextStyle title_style{
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .size = 64.0f,
            .color = Color{1, 1, 1, 1}
        };

        TextStyle sub_style{
            .font_path = "assets/fonts/Inter-Regular.ttf",
            .size = 30.0f,
            .color = Color{0.75f, 0.82f, 1.0f, 1}
        };

        s.layer("animated-card", [&](LayerBuilder& l) {
            l.from(0)
             .duration(100)
             .position({640, y, 0})
             .scale({scale_value, scale_value, 1})
             .opacity(alpha);

            l.rounded_rect("card-bg", {
                .size = {760, 260},
                .radius = 32.0f,
                .color = Color{0.10f, 0.12f, 0.22f, 1},
                .pos = {0, 0, 0}
            }).with_shadow({
                .enabled = true,
                .offset = {0, 12},
                .color = {0, 0, 0, 0.45f},
                .radius = 24
            });

            l.text("title", {
                .content = "CHRONON3D",
                .style = title_style,
                .pos = {-285, -55, 0}
            });

            l.text("subtitle", {
                .content = "programmatic motion graphics",
                .style = sub_style,
                .pos = {-285, 35, 0}
            });
        });

        return s.build();
    });
}

Composition LayerLocalCoordinatesProof() {
    return composition({
        .name = "LayerLocalCoordinatesProof",
        .width = 800,
        .height = 500,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {800, 500},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {400, 250, 0}
        });

        s.layer("layer-a", [&](LayerBuilder& l) {
            l.position({200, 250, 0});
            l.rounded_rect("card-a", {
                .size = {220, 140},
                .radius = 18,
                .color = Color{0.2f, 0.3f, 0.9f, 1},
                .pos = {0, 0, 0}
            });
            l.circle("center-a", {
                .radius = 10,
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("layer-b", [&](LayerBuilder& l) {
            l.position({600, 250, 0});
            l.rounded_rect("card-b", {
                .size = {220, 140},
                .radius = 18,
                .color = Color{0.9f, 0.25f, 0.25f, 1},
                .pos = {0, 0, 0}
            });
            l.circle("center-b", {
                .radius = 10,
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

Composition LayerOpacityProof() {
    return composition({
        .name = "LayerOpacityProof",
        .width = 800,
        .height = 500,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {800, 500},
            .color = Color{0, 0, 0, 1},
            .pos = {400, 250, 0}
        });

        s.layer("opacity-100", [&](LayerBuilder& l) {
            l.position({200, 250, 0})
             .opacity(1.0f);

            l.rect("white-full", {
                .size = {160, 160},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("opacity-50", [&](LayerBuilder& l) {
            l.position({400, 250, 0})
             .opacity(0.5f);

            l.rect("white-half", {
                .size = {160, 160},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("opacity-20", [&](LayerBuilder& l) {
            l.position({600, 250, 0})
             .opacity(0.2f);

            l.rect("white-low", {
                .size = {160, 160},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

Composition LayerDrawOrderProof() {
    return composition({
        .name = "LayerDrawOrderProof",
        .width = 600,
        .height = 400,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {600, 400},
            .color = Color{0.03f, 0.03f, 0.04f, 1},
            .pos = {300, 200, 0}
        });

        s.layer("red-layer", [&](LayerBuilder& l) {
            l.position({300, 200, 0});
            l.rect("red", {
                .size = {240, 180},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("blue-layer", [&](LayerBuilder& l) {
            l.position({340, 230, 0});
            l.rect("blue", {
                .size = {240, 180},
                .color = Color{0, 0.25f, 1, 0.85f},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

Composition LayerChildDrawOrderProof() {
    return composition({
        .name = "LayerChildDrawOrderProof",
        .width = 600,
        .height = 400,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {600, 400},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {300, 200, 0}
        });

        s.layer("group", [&](LayerBuilder& l) {
            l.position({300, 200, 0});

            l.rect("red-child", {
                .size = {260, 180},
                .color = Color{1, 0, 0, 1},
                .pos = {-30, -20, 0}
            });

            l.rect("green-child", {
                .size = {220, 150},
                .color = Color{0, 1, 0, 0.8f},
                .pos = {20, 20, 0}
            });

            l.circle("blue-child", {
                .radius = 60,
                .color = Color{0, 0.2f, 1, 0.8f},
                .pos = {55, 35, 0}
            });
        });

        return s.build();
    });
}

Composition LayerVisibilityProof() {
    return composition({
        .name = "LayerVisibilityProof",
        .width = 800,
        .height = 500,
        .duration = 40
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {800, 500},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {400, 250, 0}
        });

        s.layer("timed-layer", [&](LayerBuilder& l) {
            l.from(10)
             .duration(20)
             .position({400, 250, 0});

            l.rounded_rect("visible-card", {
                .size = {420, 180},
                .radius = 22,
                .color = Color{0.25f, 0.55f, 1.0f, 1},
                .pos = {0, 0, 0}
            });

            l.text("label", {
                .content = "VISIBLE ONLY FRAME 10-29",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 28,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-190, -12, 0}
            });
        });

        return s.build();
    });
}

Composition LayerTransformStressProof() {
    return composition({
        .name = "LayerTransformStressProof",
        .width = 900,
        .height = 600,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {900, 600},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {450, 300, 0}
        });

        s.layer("rotated-layer", [&](LayerBuilder& l) {
            l.position({450, 300, 0})
             .scale({1.2f, 1.2f, 1.0f})
             .rotate({0, 0, 12});

            l.rounded_rect("card", {
                .size = {420, 180},
                .radius = 24,
                .color = Color{0.1f, 0.18f, 0.35f, 1},
                .pos = {0, 0, 0}
            });

            l.line("x-axis", {
                .from = {-220, 0, 0},
                .to = {220, 0, 0},
                .thickness = 1,
                .color = Color{1, 0.3f, 0.3f, 1}
            });

            l.line("y-axis", {
                .from = {0, -120, 0},
                .to = {0, 120, 0},
                .thickness = 1,
                .color = Color{0.3f, 1, 0.3f, 1}
            });

            l.text("label", {
                .content = "ROTATED LAYER",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 30,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-160, -18, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LayerProof", LayerProof)
CHRONON_REGISTER_COMPOSITION("AnimatedLayerProof", AnimatedLayerProof)
CHRONON_REGISTER_COMPOSITION("LayerLocalCoordinatesProof", LayerLocalCoordinatesProof)
CHRONON_REGISTER_COMPOSITION("LayerOpacityProof", LayerOpacityProof)
CHRONON_REGISTER_COMPOSITION("LayerDrawOrderProof", LayerDrawOrderProof)
CHRONON_REGISTER_COMPOSITION("LayerChildDrawOrderProof", LayerChildDrawOrderProof)
CHRONON_REGISTER_COMPOSITION("LayerVisibilityProof", LayerVisibilityProof)
CHRONON_REGISTER_COMPOSITION("LayerTransformStressProof", LayerTransformStressProof)

} // namespace chronon3d
