#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

static const TextStyle kBold{
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 34, .color = Color{1, 1, 1, 1}
};
static const TextStyle kBoldCenter{
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 34, .color = Color{1, 1, 1, 1}, .align = TextAlign::Center
};

// ---------------------------------------------------------------------------
// TextAlignProof — Left / Center / Right alignment comparison
// ---------------------------------------------------------------------------
static Composition TextAlignProof() {
    return composition({
        .name = "TextAlignProof", .width = 900, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {900, 500}, .color = Color{0.05f, 0.05f, 0.08f, 1}, .pos = {450, 250, 0} });

        const TextStyle left_style  { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 38,
                                      .color = Color{1, 0.6f, 0.3f, 1}, .align = TextAlign::Left };
        const TextStyle center_style{ .font_path = "assets/fonts/Inter-Bold.ttf", .size = 38,
                                      .color = Color{0.3f, 1, 0.5f, 1}, .align = TextAlign::Center };
        const TextStyle right_style { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 38,
                                      .color = Color{0.4f, 0.7f, 1, 1}, .align = TextAlign::Right };

        s.layer("labels", [&](LayerBuilder& l) {
            l.position({450, 250, 0});
            l.text("left",   { .content = "LEFT ALIGNED TEXT",   .style = left_style,   .pos = {0, -100, 0} });
            l.text("center", { .content = "CENTER ALIGNED TEXT", .style = center_style, .pos = {0,    0, 0} });
            l.text("right",  { .content = "RIGHT ALIGNED TEXT",  .style = right_style,  .pos = {0,  100, 0} });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("TextAlignProof", TextAlignProof)

// ---------------------------------------------------------------------------
// BlurProof — progressive blur levels
// ---------------------------------------------------------------------------
static Composition BlurProof() {
    return composition({
        .name = "BlurProof", .width = 1100, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {1100, 500}, .color = Color{0.03f, 0.03f, 0.05f, 1}, .pos = {550, 250, 0} });

        auto card = [&](f32 x, f32 blur_r, const char* label) {
            s.layer("card", [&](LayerBuilder& l) {
                l.position({x, 250, 0}).blur(blur_r);
                l.rounded_rect("r", { .size = {200, 280}, .radius = 20,
                    .color = Color{0.2f, 0.45f, 0.9f, 1}, .pos = {0, 0, 0} });
                l.image("img", { .path = "assets/images/checker.png",
                    .size = {180, 180}, .pos = {0, -30, 0}, .opacity = 1 });
                l.text("lbl", { .content = label, .style = kBoldCenter, .pos = {0, 100, 0} });
            });
        };

        card(150,  0,  "NO BLUR");
        card(400,  3,  "BLUR 3");
        card(650,  8,  "BLUR 8");
        card(900, 18, "BLUR 18");
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("BlurProof", BlurProof)

// ---------------------------------------------------------------------------
// TintProof — color tint overlay
// ---------------------------------------------------------------------------
static Composition TintProof() {
    return composition({
        .name = "TintProof", .width = 900, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {900, 500}, .color = Color{0.03f, 0.03f, 0.05f, 1}, .pos = {450, 250, 0} });

        struct T { f32 x; Color tint; const char* label; };
        for (auto [x, tint, label] : std::initializer_list<T>{
            {150, Color{0,0,0,0},            "NO TINT"},
            {375, Color{1, 0.3f, 0.1f, 0.6f}, "RED TINT"},
            {600, Color{0.1f, 0.5f, 1, 0.6f}, "BLUE TINT"},
            {825, Color{0, 0, 0, 0.7f},        "DARK TINT"},
        }) {
            s.layer("card", [&](LayerBuilder& l) {
                l.position({x, 250, 0}).tint(tint);
                l.image("img", { .path = "assets/images/checker.png",
                    .size = {180, 240}, .pos = {0, -30, 0}, .opacity = 1 });
                l.text("lbl", { .content = label, .style = kBoldCenter, .pos = {0, 110, 0} });
            });
        }
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("TintProof", TintProof)

// ---------------------------------------------------------------------------
// BrightnessContrastProof
// ---------------------------------------------------------------------------
static Composition BrightnessContrastProof() {
    return composition({
        .name = "BrightnessContrastProof", .width = 1000, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {1000, 500}, .color = Color{0.03f, 0.03f, 0.05f, 1}, .pos = {500, 250, 0} });

        struct C { f32 x; f32 bright; f32 contr; const char* label; };
        for (auto [x, bright, contr, label] : std::initializer_list<C>{
            {100,  0,    1,    "NORMAL"},
            {300,  0.3f, 1,    "BRIGHT"},
            {500, -0.3f, 1,    "DARK"},
            {700,  0,    1.8f, "HIGH CONT"},
            {900,  0,    0.4f, "LOW CONT"},
        }) {
            s.layer("card", [&](LayerBuilder& l) {
                l.position({x, 250, 0}).brightness(bright).contrast(contr);
                l.image("img", { .path = "assets/images/checker.png",
                    .size = {160, 210}, .pos = {0, -35, 0}, .opacity = 1 });
                l.text("lbl", { .content = label, .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf", .size = 22,
                    .color = Color{1,1,1,1}, .align = TextAlign::Center
                }, .pos = {0, 105, 0} });
            });
        }
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("BrightnessContrastProof", BrightnessContrastProof)

// ---------------------------------------------------------------------------
// BlendModeProof — Normal / Screen / Multiply / Overlay / Add
// ---------------------------------------------------------------------------
static Composition BlendModeProof() {
    return composition({
        .name = "BlendModeProof", .width = 1100, .height = 500, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Colourful background
        s.rect("bg", { .size = {1100, 500}, .color = Color{0.8f, 0.5f, 0.1f, 1}, .pos = {550, 250, 0} });
        s.rect("stripe", { .size = {1100, 120}, .color = Color{0.1f, 0.3f, 0.9f, 1}, .pos = {550, 250, 0} });

        struct B { f32 x; BlendMode mode; const char* label; };
        for (auto [x, mode, label] : std::initializer_list<B>{
            {110, BlendMode::Normal,   "NORMAL"},
            {310, BlendMode::Screen,   "SCREEN"},
            {510, BlendMode::Multiply, "MULTIPLY"},
            {710, BlendMode::Overlay,  "OVERLAY"},
            {910, BlendMode::Add,      "ADD"},
        }) {
            s.layer("card", [&](LayerBuilder& l) {
                l.position({x, 250, 0}).blend(mode);
                l.rounded_rect("r", { .size = {170, 320}, .radius = 18,
                    .color = Color{0.2f, 0.7f, 0.4f, 0.85f}, .pos = {0, 0, 0} });
                l.text("lbl", { .content = label, .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf", .size = 24,
                    .color = Color{1,1,1,1}, .align = TextAlign::Center
                }, .pos = {0, 145, 0} });
            });
        }
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("BlendModeProof", BlendModeProof)

// ---------------------------------------------------------------------------
// AnimatedBlurRevealProof — blur dissolves from 20 to 0 as card reveals
// ---------------------------------------------------------------------------
static Composition AnimatedBlurRevealProof() {
    return composition({
        .name = "AnimatedBlurRevealProof", .width = 1280, .height = 720, .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.01f, 0.01f, 0.02f, 1}, .pos = {640, 360, 0} });

        const auto blur_r  = interpolate(ctx.frame, 0, 60,  20.0f, 0.0f);
        const auto opacity = interpolate(ctx.frame, 0, 60,   0.0f, 1.0f);

        s.layer("card", [&](LayerBuilder& l) {
            l.position({640, 360, 0}).blur(blur_r).opacity(opacity);
            l.rounded_rect("bg", { .size = {700, 360}, .radius = 36,
                .color = Color{0.1f, 0.15f, 0.35f, 1}, .pos = {0, 0, 0} });
            l.text("title", { .content = "BLUR REVEAL",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 62,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -30, 0} });
            l.text("sub", { .content = "effects stack in motion",
                .style = { .font_path = "assets/fonts/Inter-Regular.ttf", .size = 30,
                           .color = Color{0.7f, 0.8f, 1, 1}, .align = TextAlign::Center },
                .pos = {0, 50, 0} });
        });
        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("AnimatedBlurRevealProof", AnimatedBlurRevealProof)
