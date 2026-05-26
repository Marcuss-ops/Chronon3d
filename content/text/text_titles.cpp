#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <string>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

struct TitleSpec {
    std::string text;
    f32 size{100.0f};
    f32 tracking{12.0f};
    Color color{1, 1, 1, 1};
    std::string font{"assets/fonts/Georgia_Bold.ttf"};

    TitleSpec(std::string t) : text(std::move(t)) {}

    TitleSpec& set_size(f32 s) { size = s; return *this; }
    TitleSpec& set_tracking(f32 t) { tracking = t; return *this; }
    TitleSpec& set_color(Color c) { color = c; return *this; }
    TitleSpec& set_font(std::string f) { font = std::move(f); return *this; }

    void draw(LayerBuilder& l) const {
        apply_text(l, "title_text", {
            .text = text,
            .size = size,
            .color = color,
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .tracking = tracking,
            .font_path = font,
        }, {W * 0.72f, 160.0f});
    }
};

} // namespace

Composition text_perspective_sweep_demo() {
    return composition({
        .name = "TextPerspectiveSweepDemo",
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        TitleSpec title = TitleSpec("LIL DIRK").set_size(100.0f).set_tracking(12.0f).set_font("assets/fonts/Georgia_Bold.ttf");

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color::black()).cache_static();
        });

        s.layer("demo_title", [&](LayerBuilder& l) {
            camera_motion::apply_dolly_pitch_sweep(s, l, static_cast<int>(ctx.duration));
            title.draw(l);
            l.with_glow({.enabled = true, .radius = 20.0f, .intensity = 0.4f});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
