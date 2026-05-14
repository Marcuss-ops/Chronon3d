#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <fmt/format.h>

namespace chronon3d {

Composition ImageProof() {
    return composition({
        .name = "ImageProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {640, 360, 0}
        });

        s.layer("card", [&](LayerBuilder& l) {
            l.position({640, 360, 0});

            l.rounded_rect("card-bg", {
                .size = {820, 320},
                .radius = 32,
                .color = Color{0.10f, 0.12f, 0.22f, 1},
                .pos = {0, 0, 0}
            }).with_shadow({
                .enabled = true,
                .offset = {0, 12},
                .color = {0, 0, 0, 0.45f},
                .radius = 24
            });

            l.image("test-image", {
                .path = "assets/images/checker.png",
                .size = {220, 220},
                .pos = {-250, 0, 0},
                .opacity = 1.0f
            });

            l.text("title", {
                .content = "IMAGE RENDERING",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 44,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-60, -45, 0}
            });

            l.text("subtitle", {
                .content = "PNG assets inside layers",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Regular.ttf",
                    .size = 28,
                    .color = Color{0.75f, 0.82f, 1.0f, 1}
                },
                .pos = {-60, 20, 0}
            });
        });

        return s.build();
    });
}

Composition AnimatedImageProof() {
    return composition({
        .name = "AnimatedImageProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.015f, 0.015f, 0.025f, 1},
            .pos = {640, 360, 0}
        });

        auto intro = sequence(ctx, 0, 70);
        auto y = spring(intro, -180.0f, 360.0f);
        auto alpha = interpolate(ctx.frame, 0, 25, 0.0f, 1.0f);
        auto scale_value = interpolate(ctx.frame, 0, 45, 0.85f, 1.0f, Easing::OutCubic);

        s.layer("animated-image-card", [&](LayerBuilder& l) {
            l.position({640, y, 0})
             .scale({scale_value, scale_value, 1})
             .opacity(alpha);

            l.rounded_rect("card-bg", {
                .size = {820, 320},
                .radius = 32,
                .color = Color{0.10f, 0.12f, 0.22f, 1},
                .pos = {0, 0, 0}
            }).with_shadow({
                .enabled = true,
                .offset = {0, 12},
                .color = {0, 0, 0, 0.45f},
                .radius = 24
            });

            l.image("asset", {
                .path = "assets/images/checker.png",
                .size = {220, 220},
                .pos = {-250, 0, 0},
                .opacity = 1.0f
            });

            l.text("title", {
                .content = "IMAGE + LAYER",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 48,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-60, -50, 0}
            });

            l.text("subtitle", {
                .content = "ready for real graphics",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Regular.ttf",
                    .size = 30,
                    .color = Color{0.75f, 0.82f, 1.0f, 1}
                },
                .pos = {-60, 25, 0}
            });
        });

        return s.build();
    });
}

// Special composition to generate the checkerboard asset if missing
Composition CheckerGen() {
    return composition({
        .name = "CheckerGen",
        .width = 256,
        .height = 256,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const int tileSize = 32;
        for (int y = 0; y < 256; y += tileSize) {
            for (int x = 0; x < 256; x += tileSize) {
                bool isWhite = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                s.rect(fmt::format("tile_{}_{}", x, y), {
                    .size = {(f32)tileSize, (f32)tileSize},
                    .color = isWhite ? Color{1, 1, 1, 1} : Color{0.2f, 0.4f, 0.8f, 1},
                    .pos = {(f32)x + tileSize/2.0f, (f32)y + tileSize/2.0f, 0}
                });
            }
        }
        return s.build();
    });
}


Composition ImageMissingProof() {
    return composition({
        .name = "ImageMissingProof",
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

        s.rounded_rect("card", {
            .size = {500, 220},
            .radius = 24,
            .color = Color{0.12f, 0.14f, 0.24f, 1},
            .pos = {400, 250, 0}
        });

        s.image("missing-image", {
            .path = "assets/images/does_not_exist.png",
            .size = {200, 140},
            .pos = {400, 230, 0},
            .opacity = 1.0f
        });

        s.text("label", {
            .content = "MISSING IMAGE SHOULD NOT CRASH",
            .style = TextStyle{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 26,
                .color = Color{1, 1, 1, 1}
            },
            .pos = {190, 330, 0}
        });

        return s.build();
    });
}

Composition ImageAlphaProof() {
    return composition({
        .name = "ImageAlphaProof",
        .width = 900,
        .height = 500,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {900, 500},
            .color = Color{0, 0, 0, 1},
            .pos = {450, 250, 0}
        });

        s.image("img-100", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {220, 250, 0},
            .opacity = 1.0f
        });

        s.image("img-50", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {450, 250, 0},
            .opacity = 0.5f
        });

        s.image("img-20", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {680, 250, 0},
            .opacity = 0.2f
        });

        return s.build();
    });
}

Composition ImageClippingProof() {
    return composition({
        .name = "ImageClippingProof",
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

        s.image("left", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {-40, 250, 0},
            .opacity = 1.0f
        });

        s.image("right", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {840, 250, 0},
            .opacity = 1.0f
        });

        s.image("top", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {400, -40, 0},
            .opacity = 1.0f
        });

        s.image("bottom", {
            .path = "assets/images/checker.png",
            .size = {180, 180},
            .pos = {400, 540, 0},
            .opacity = 1.0f
        });

        return s.build();
    });
}

Composition ImageLayerTransformProof() {
    return composition({
        .name = "ImageLayerTransformProof",
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

        s.layer("image-layer", [&](LayerBuilder& l) {
            l.position({450, 300, 0})
             .scale({1.25f, 1.25f, 1.0f})
             .rotate({0, 0, 12})
             .opacity(0.85f);

            l.rounded_rect("card-bg", {
                .size = {420, 260},
                .radius = 24,
                .color = Color{0.12f, 0.14f, 0.24f, 1},
                .pos = {0, 0, 0}
            });

            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {180, 180},
                .pos = {0, 0, 0},
                .opacity = 1.0f
            });

            l.line("x-axis", {
                .from = {-240, 0, 0},
                .to = {240, 0, 0},
                .thickness = 1,
                .color = Color{1, 0.3f, 0.3f, 1}
            });
        });

        return s.build();
    });
}

Composition ImageDrawOrderProof() {
    return composition({
        .name = "ImageDrawOrderProof",
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

        s.layer("order", [&](LayerBuilder& l) {
            l.position({400, 250, 0});

            l.image("image-bottom", {
                .path = "assets/images/checker.png",
                .size = {260, 260},
                .pos = {0, 0, 0},
                .opacity = 1.0f
            });

            l.rect("red-overlay", {
                .size = {220, 120},
                .color = Color{1, 0, 0, 0.55f},
                .pos = {0, 0, 0}
            });

            l.text("text-top", {
                .content = "TEXT ON TOP",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 32,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-115, -15, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ImageProof", ImageProof)
CHRONON_REGISTER_COMPOSITION("AnimatedImageProof", AnimatedImageProof)
CHRONON_REGISTER_COMPOSITION("CheckerGen", CheckerGen)
CHRONON_REGISTER_COMPOSITION("ImageMissingProof", ImageMissingProof)
CHRONON_REGISTER_COMPOSITION("ImageAlphaProof", ImageAlphaProof)
CHRONON_REGISTER_COMPOSITION("ImageClippingProof", ImageClippingProof)
CHRONON_REGISTER_COMPOSITION("ImageLayerTransformProof", ImageLayerTransformProof)
CHRONON_REGISTER_COMPOSITION("ImageDrawOrderProof", ImageDrawOrderProof)

} // namespace chronon3d
