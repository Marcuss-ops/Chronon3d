#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

/**
 * BloomProof
 *
 * Verifies bloom post-process: emissive white panel on dark background.
 * An adjustment layer with bloom(0.78, 28, 0.70) is applied after all layers.
 * Expected: soft halo around bright panel, background stays dark.
 */
Composition BloomProof() {
    return composition({
        .name     = "BloomProof",
        .width    = 1280,
        .height   = 720,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Dark background
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.03f, 0.03f, 0.05f, 1.0f}});
        });

        // Emissive white panel (very bright — will trigger bloom)
        s.layer("panel", [](LayerBuilder& l) {
            l.rounded_rect("p", {
                .size   = {500, 260},
                .radius = 22,
                .color  = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .pos    = {640, 360, 0}
            });
        });

        // Orange accent — moderately bright
        s.layer("accent", [](LayerBuilder& l) {
            l.rounded_rect("a", {
                .size   = {180, 60},
                .radius = 12,
                .color  = Color{0.95f, 0.55f, 0.10f, 1.0f},
                .pos    = {640, 480, 0}
            });
        });

        // Title text
        s.layer("title", [](LayerBuilder& l) {
            l.text("t", {
                .content = "BLOOM",
                .style   = {.font_path = "assets/fonts/Inter-Bold.ttf",
                            .size = 72, .color = Color{0.08f, 0.06f, 0.12f, 1.0f},
                            .align = TextAlign::Center},
                .pos     = {640, 346, 0}
            });
        });

        // Global bloom adjustment layer — applies to everything rendered before it
        s.adjustment_layer("bloom_adj", [](LayerBuilder& l) {
            l.bloom(0.78f, 28.0f, 0.70f);
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("BloomProof", BloomProof)
