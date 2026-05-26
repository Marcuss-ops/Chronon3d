#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <string>
#include <cmath>

#include "text_helpers.hpp"
#include <chronon3d/animation/interpolate.hpp>

namespace chronon3d::content::text {

Composition text_perspective_sweep_demo() {
    return composition({
        .name = "TextPerspectiveSweepDemo",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
            l.cache_static();
        });

        s.layer("demo_title", [&](LayerBuilder& l) {
            camera_motion::apply_dolly_pitch_sweep(s, l, static_cast<int>(ctx.duration));

            apply_text(l, "title_text", {
                .text = "LIL DIRK",
                .size = 100.0f,
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 12.0f,
                .font_path = "assets/fonts/Georgia_Bold.ttf",
            }, {W * 0.72f, 160.0f}, {0.0f, 0.0f, 0.0f});

            l.with_glow({.enabled = true, .radius = 20.0f, .intensity = 0.4f, .color = Color::white()});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
