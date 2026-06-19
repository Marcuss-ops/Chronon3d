// content/effects/glow_basic_word.cpp
// Basic text preset: one centered word on a black background.
#include "../common/glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_basic_word() {
    return composition({.name = "GlowBasicWord", .width = kW, .height = kH, .duration = 90},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.0f, 0.0f, 0.0f, 1.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});

        s.layer("word", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(1.0f);
            l.position({0.0f, 0.0f, 0.0f});

            l.text("t", TextSpec{
                .content = {.value = "GLOW"},
                .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_size = 118.0f},
                .layout = {.box = {620.0f, 160.0f}, .anchor = TextAnchor::Center, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = 12.0f},
                .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},
                .position = {0.0f, 0.0f, 0.0f}
            });
        });
        return s.build();
    });
}

} // namespace chronon3d::content::effects
