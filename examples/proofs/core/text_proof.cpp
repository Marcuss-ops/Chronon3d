#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d {

Composition TextProof() {
    return composition({
        .name = "TextProof",
        .width = 1280,
        .height = 720,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        
        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1},
            .pos = {640, 360, 0}
        });

        s.rounded_rect("card", {
            .size = {760, 260},
            .radius = 32,
            .color = Color{0.10f, 0.12f, 0.20f, 1},
            .pos = {640, 360, 0}
        })
        .with_shadow({.enabled = true, .offset = {0, 10}, .color = {0, 0, 0, 0.5f}, .radius = 20});

        s.text("title", {
            .content = "CHRONON3D",
            .style = {"assets/fonts/Inter-Bold.ttf", 72.0f, {1, 1, 1, 1}},
            .pos = {330, 270, 0}
        });

        s.text("subtitle", {
            .content = "Code-first motion graphics",
            .style = {"assets/fonts/Inter-Regular.ttf", 34.0f, {0.75f, 0.80f, 1.0f, 1}},
            .pos = {335, 360, 0}
        });

        return s.build();
    });
}

Composition TextAlphaProof() {
    return composition({
        .name = "TextAlphaProof",
        .width = 800,
        .height = 450,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", { .size = {800, 450}, .color = Color{0.1f, 0.1f, 0.1f, 1}, .pos = {400, 225, 0} });
        
        s.text("a100", { .content = "TEST ALPHA 100 percent", .style = {"assets/fonts/Inter-Regular.ttf", 48, {1, 1, 1, 1.0f}}, .pos = {50, 100, 0} });
        s.text("a50",  { .content = "TEST ALPHA 50 percent",  .style = {"assets/fonts/Inter-Regular.ttf", 48, {1, 1, 1, 0.5f}}, .pos = {50, 200, 0} });
        s.text("a20",  { .content = "TEST ALPHA 20 percent",  .style = {"assets/fonts/Inter-Regular.ttf", 48, {1, 1, 1, 0.2f}}, .pos = {50, 300, 0} });
        
        return s.build();
    });
}

Composition TextDrawOrderProof() {
    return composition({
        .name = "TextDrawOrderProof",
        .width = 800,
        .height = 450,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", { .size = {800, 450}, .color = Color{0.1f, 0.1f, 0.1f, 1}, .pos = {400, 225, 0} });
        s.rounded_rect("card", { .size = {500, 200}, .radius = 20, .color = Color{0.2f, 0.2f, 0.3f, 1}, .pos = {400, 225, 0} });
        s.text("text", { .content = "ORDER TEST", .style = {"assets/fonts/Inter-Bold.ttf", 64, {1, 1, 1, 1}}, .pos = {220, 210, 0} });
        s.rect("overlay", { .size = {200, 100}, .color = Color{1, 0, 0, 0.4f}, .pos = {400, 225, 0} });
        return s.build();
    });
}

Composition TextClippingProof() {
    return composition({
        .name = "TextClippingProof",
        .width = 400,
        .height = 300,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", { .size = {400, 300}, .color = Color{0.05f, 0.05f, 0.05f, 1}, .pos = {200, 150, 0} });
        s.text("clip-left",   { .content = "CLIPPING LEFT",   .style = {"assets/fonts/Inter-Regular.ttf", 40, {1, 1, 1, 1}}, .pos = {-100, 50, 0} });
        s.text("clip-right",  { .content = "CLIPPING RIGHT",  .style = {"assets/fonts/Inter-Regular.ttf", 40, {1, 1, 1, 1}}, .pos = {250, 120, 0} });
        s.text("clip-top",    { .content = "CLIPPING TOP",    .style = {"assets/fonts/Inter-Regular.ttf", 40, {1, 1, 1, 1}}, .pos = {100, -10, 0} });
        s.text("clip-bottom", { .content = "CLIPPING BOTTOM", .style = {"assets/fonts/Inter-Regular.ttf", 40, {1, 1, 1, 1}}, .pos = {50, 280, 0} });
        return s.build();
    });
}

Composition TextSizesProof() {
    return composition({
        .name = "TextSizesProof",
        .width = 800,
        .height = 600,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", { .size = {800, 600}, .color = Color{0.1f, 0.1f, 0.1f, 1}, .pos = {400, 300, 0} });
        float y = 50;
        for (float size : {16.0f, 24.0f, 36.0f, 48.0f, 72.0f, 96.0f}) {
            s.text("s" + std::to_string((int)size), {
                .content = "Size " + std::to_string((int)size),
                .style = {"assets/fonts/Inter-Regular.ttf", size, {1, 1, 1, 1}},
                .pos = {50, y, 0}
            });
            y += size + 10;
        }
        return s.build();
    });
}

Composition TextMissingFontProof() {
    return composition({
        .name = "TextMissingFontProof",
        .width = 400,
        .height = 300,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", { .size = {400, 300}, .color = Color{0.1f, 0.1f, 0.1f, 1}, .pos = {200, 150, 0} });
        s.text("missing", { .content = "SHOULD NOT CRASH", .style = {"assets/fonts/non_existent.ttf", 32, {1, 1, 1, 1}}, .pos = {50, 150, 0} });
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TextProof", TextProof)
CHRONON_REGISTER_COMPOSITION("TextAlphaProof", TextAlphaProof)
CHRONON_REGISTER_COMPOSITION("TextDrawOrderProof", TextDrawOrderProof)
CHRONON_REGISTER_COMPOSITION("TextClippingProof", TextClippingProof)
CHRONON_REGISTER_COMPOSITION("TextSizesProof", TextSizesProof)
CHRONON_REGISTER_COMPOSITION("TextMissingFontProof", TextMissingFontProof)

} // namespace chronon3d
