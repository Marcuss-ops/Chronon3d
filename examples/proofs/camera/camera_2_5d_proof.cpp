#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// Camera25DDepthScaleProof
// Far card smaller, normal card medium, near card larger.
// ---------------------------------------------------------------------------
Composition Camera25DDepthScaleProof() {
    return composition({
        .name = "Camera25DDepthScaleProof",
        .width = 1280,
        .height = 720,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {640, 360, 0}
        });

        TextStyle label{
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .size = 26,
            .color = Color{1, 1, 1, 1}
        };

        s.layer("far", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({320, 360, 600});
            l.rounded_rect("card", {
                .size = {260, 160},
                .radius = 20,
                .color = Color{0.15f, 0.25f, 0.75f, 1},
                .pos = {0, 0, 0}
            });
            l.text("label", {
                .content = "FAR Z +600",
                .style = label,
                .pos = {-90, -14, 0}
            });
        });

        s.layer("normal", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0});
            l.rounded_rect("card", {
                .size = {260, 160},
                .radius = 20,
                .color = Color{0.20f, 0.60f, 0.30f, 1},
                .pos = {0, 0, 0}
            });
            l.text("label", {
                .content = "Z 0",
                .style = label,
                .pos = {-30, -14, 0}
            });
        });

        s.layer("near", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({960, 360, -400});
            l.rounded_rect("card", {
                .size = {260, 160},
                .radius = 20,
                .color = Color{0.85f, 0.25f, 0.20f, 1},
                .pos = {0, 0, 0}
            });
            l.text("label", {
                .content = "NEAR Z -400",
                .style = label,
                .pos = {-105, -14, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DDepthScaleProof", Camera25DDepthScaleProof)

// ---------------------------------------------------------------------------
// Camera25DParallaxProof
// Camera pans X; near layer shifts more than far layer.
// ---------------------------------------------------------------------------
Composition Camera25DParallaxProof() {
    return composition({
        .name = "Camera25DParallaxProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        auto cam_x = interpolate(ctx.frame, 0, 60, -180.0f, 180.0f);

        s.camera_2_5d({
            .enabled = true,
            .position = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg-flat", {
            .size = {1280, 720},
            .color = Color{0.01f, 0.01f, 0.02f, 1},
            .pos = {640, 360, 0}
        });

        s.layer("far-bg", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 800});
            l.image("far-image", {
                .path = "assets/images/checker.png",
                .size = {900, 500},
                .pos = {0, 0, 0},
                .opacity = 0.45f
            });
        });

        s.layer("middle-card", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0});
            l.rounded_rect("card", {
                .size = {520, 240},
                .radius = 28,
                .color = Color{0.10f, 0.14f, 0.26f, 1},
                .pos = {0, 0, 0}
            });
            l.text("title", {
                .content = "MIDDLE",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 48,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-110, -30, 0}
            });
        });

        s.layer("near-title", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 250, -350});
            l.text("near", {
                .content = "NEAR LAYER",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 52,
                    .color = Color{1.0f, 0.82f, 0.28f, 1}
                },
                .pos = {-150, -30, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DParallaxProof", Camera25DParallaxProof)

// ---------------------------------------------------------------------------
// Camera25DZOrderProof
// Near red inserted first but must render on top of far blue.
// ---------------------------------------------------------------------------
Composition Camera25DZOrderProof() {
    return composition({
        .name = "Camera25DZOrderProof",
        .width = 800,
        .height = 500,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {800, 500},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {400, 250, 0}
        });

        s.layer("near-red", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({400, 250, -300});
            l.rect("red", {
                .size = {260, 180},
                .color = Color{1, 0, 0, 0.80f},
                .pos = {0, 0, 0}
            });
        });

        s.layer("far-blue", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({430, 280, 400});
            l.rect("blue", {
                .size = {260, 180},
                .color = Color{0, 0.2f, 1, 0.80f},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DZOrderProof", Camera25DZOrderProof)

// ---------------------------------------------------------------------------
// Camera25DCameraPushProof
// Camera moves closer on Z; card should grow larger.
// ---------------------------------------------------------------------------
Composition Camera25DCameraPushProof() {
    return composition({
        .name = "Camera25DCameraPushProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        auto cam_z = interpolate(ctx.frame, 0, 60, -1400.0f, -650.0f);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, cam_z},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.01f, 0.01f, 0.02f, 1},
            .pos = {640, 360, 0}
        });

        s.layer("main-card", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0});
            l.rounded_rect("card", {
                .size = {560, 260},
                .radius = 32,
                .color = Color{0.12f, 0.14f, 0.28f, 1},
                .pos = {0, 0, 0}
            });
            l.text("title", {
                .content = "CAMERA PUSH",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 52,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-190, -30, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DCameraPushProof", Camera25DCameraPushProof)

// ---------------------------------------------------------------------------
// Camera25DMixed2D3DProof
// Camera pans X. 3D card follows; 2D HUD must remain fixed.
// ---------------------------------------------------------------------------
Composition Camera25DMixed2D3DProof() {
    return composition({
        .name = "Camera25DMixed2D3DProof",
        .width = 1000,
        .height = 600,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        auto cam_x = interpolate(ctx.frame, 0, 59, -200.0f, 200.0f);

        s.camera_2_5d({
            .enabled = true,
            .position = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {1000, 600},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {500, 300, 0}
        });

        s.layer("3d-card", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({500, 300, 0});
            l.rounded_rect("card", {
                .size = {360, 180},
                .radius = 24,
                .color = Color{0.20f, 0.35f, 0.85f, 1},
                .pos = {0, 0, 0}
            });
        });

        // 2D HUD — no enable_3d; camera must not affect it.
        s.layer("hud-2d", [](LayerBuilder& l) {
            l.position({500, 60, 0});
            l.text("hud", {
                .content = "2D HUD SHOULD STAY FIXED",
                .style = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 26,
                    .color = Color{1, 1, 1, 1}
                },
                .pos = {-210, -12, 0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DMixed2D3DProof", Camera25DMixed2D3DProof)

// ---------------------------------------------------------------------------
// Camera25DImageParallaxProof
// Two images at different Z; near image shifts more as camera pans.
// ---------------------------------------------------------------------------
Composition Camera25DImageParallaxProof() {
    return composition({
        .name = "Camera25DImageParallaxProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        auto cam_x = interpolate(ctx.frame, 0, 60, -160.0f, 160.0f);

        s.camera_2_5d({
            .enabled = true,
            .position = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.01f, 0.01f, 0.02f, 1},
            .pos = {640, 360, 0}
        });

        s.layer("far-image", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 700});
            l.image("checker-far", {
                .path = "assets/images/checker.png",
                .size = {900, 500},
                .pos = {0, 0, 0},
                .opacity = 0.35f
            });
        });

        s.layer("near-image", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, -250});
            l.image("checker-near", {
                .path = "assets/images/checker.png",
                .size = {320, 320},
                .pos = {0, 0, 0},
                .opacity = 1.0f
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Camera25DImageParallaxProof", Camera25DImageParallaxProof)
