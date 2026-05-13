#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

static const TextStyle kLabelBold{
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size      = 36,
    .color     = Color{1, 1, 1, 1}
};

// ---------------------------------------------------------------------------
// MaskRectProof — checker clipped inside a rect
// ---------------------------------------------------------------------------
static Composition MaskRectProof() {
    return composition({
        .name = "MaskRectProof", .width = 900, .height = 600, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {900, 600}, .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {450, 300, 0} });

        s.layer("masked", [](LayerBuilder& l) {
            l.position({450, 300, 0})
             .mask_rect({ .size = {420, 220}, .pos = {0, 0, 0} });
            l.image("big-image", {
                .path = "assets/images/checker.png",
                .size = {700, 420}, .pos = {0, 0, 0}, .opacity = 1.0f
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskRectProof", MaskRectProof)

// ---------------------------------------------------------------------------
// MaskRoundedRectProof — checker + text clipped inside rounded rect
// ---------------------------------------------------------------------------
static Composition MaskRoundedRectProof() {
    return composition({
        .name = "MaskRoundedRectProof", .width = 900, .height = 600, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {900, 600}, .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {450, 300, 0} });

        s.layer("rounded-mask", [](LayerBuilder& l) {
            l.position({450, 300, 0})
             .mask_rounded_rect({ .size = {500, 300}, .radius = 42, .pos = {0, 0, 0} });
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {680, 420}, .pos = {0, 0, 0}, .opacity = 1.0f
            });
            l.text("label", {
                .content = "ROUNDED MASK",
                .style   = kLabelBold,
                .pos     = {-175, -20, 0}
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskRoundedRectProof", MaskRoundedRectProof)

// ---------------------------------------------------------------------------
// MaskCircleProof — checker clipped to circle
// ---------------------------------------------------------------------------
static Composition MaskCircleProof() {
    return composition({
        .name = "MaskCircleProof", .width = 800, .height = 600, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {800, 600}, .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {400, 300, 0} });

        s.layer("circle-mask", [](LayerBuilder& l) {
            l.position({400, 300, 0})
             .mask_circle({ .radius = 160, .pos = {0, 0, 0} });
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {520, 520}, .pos = {0, 0, 0}, .opacity = 1.0f
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskCircleProof", MaskCircleProof)

// ---------------------------------------------------------------------------
// MaskTextRevealProof — long text visible only through a rect window
// ---------------------------------------------------------------------------
static Composition MaskTextRevealProof() {
    return composition({
        .name = "MaskTextRevealProof", .width = 1000, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {1000, 500}, .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {500, 250, 0} });

        s.layer("text-mask", [](LayerBuilder& l) {
            l.position({500, 250, 0})
             .mask_rect({ .size = {420, 90}, .pos = {0, 0, 0} });
            l.text("big-text", {
                .content = "THIS TEXT IS CLIPPED BY A MASK",
                .style   = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 52, .color = Color{1, 1, 1, 1}
                },
                .pos = {-450, -30, 0}
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskTextRevealProof", MaskTextRevealProof)

// ---------------------------------------------------------------------------
// AnimatedMaskRevealProof — width grows from 0 to full over 60 frames
// ---------------------------------------------------------------------------
static Composition AnimatedMaskRevealProof() {
    return composition({
        .name = "AnimatedMaskRevealProof", .width = 1280, .height = 720, .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.015f, 0.015f, 0.025f, 1}, .pos = {640, 360, 0} });

        const auto reveal_w = interpolate(ctx.frame, 0, 60, 0.0f, 720.0f);

        s.layer("reveal-card", [&](LayerBuilder& l) {
            l.position({640, 360, 0})
             .mask_rounded_rect({ .size = {reveal_w, 300}, .radius = 32, .pos = {0, 0, 0} });

            l.rounded_rect("card-bg", {
                .size = {720, 300}, .radius = 32,
                .color = Color{0.10f, 0.14f, 0.28f, 1}, .pos = {0, 0, 0}
            });
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {220, 220}, .pos = {-230, 0, 0}, .opacity = 1.0f
            });
            l.text("title", {
                .content = "MASK REVEAL",
                .style   = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 54, .color = Color{1, 1, 1, 1}
                },
                .pos = {-40, -45, 0}
            });
            l.text("subtitle", {
                .content = "animated layer clipping",
                .style   = TextStyle{
                    .font_path = "assets/fonts/Inter-Regular.ttf",
                    .size = 30, .color = Color{0.75f, 0.82f, 1.0f, 1}
                },
                .pos = {-40, 35, 0}
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("AnimatedMaskRevealProof", AnimatedMaskRevealProof)

// ---------------------------------------------------------------------------
// MaskLayerTransformProof — mask follows layer scale + rotation
// ---------------------------------------------------------------------------
static Composition MaskLayerTransformProof() {
    return composition({
        .name = "MaskLayerTransformProof", .width = 900, .height = 600, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {900, 600}, .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {450, 300, 0} });

        s.layer("transformed-mask", [](LayerBuilder& l) {
            l.position({450, 300, 0})
             .scale({1.2f, 1.2f, 1.0f})
             .rotate({0, 0, 12})
             .mask_rounded_rect({ .size = {420, 260}, .radius = 36, .pos = {0, 0, 0} });
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {620, 420}, .pos = {0, 0, 0}, .opacity = 1.0f
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskLayerTransformProof", MaskLayerTransformProof)

// ---------------------------------------------------------------------------
// MaskCamera25DProof — masked layer in 2.5D space
// ---------------------------------------------------------------------------
static Composition MaskCamera25DProof() {
    return composition({
        .name = "MaskCamera25DProof", .width = 1280, .height = 720, .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const auto cam_x = interpolate(ctx.frame, 0, 60, -120.0f, 120.0f);

        s.camera_2_5d({
            .enabled          = true,
            .position         = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom             = 1000.0f
        });

        s.rect("bg", { .size = {1280, 720}, .color = Color{0.01f, 0.01f, 0.02f, 1}, .pos = {640, 360, 0} });

        s.layer("masked-3d", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, -200})
             .mask_rounded_rect({ .size = {560, 320}, .radius = 36, .pos = {0, 0, 0} });
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {760, 460}, .pos = {0, 0, 0}, .opacity = 1.0f
            });
            l.text("title", {
                .content = "2.5D MASK",
                .style   = TextStyle{
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 50, .color = Color{1, 1, 1, 1}
                },
                .pos = {-155, -30, 0}
            });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("MaskCamera25DProof", MaskCamera25DProof)
