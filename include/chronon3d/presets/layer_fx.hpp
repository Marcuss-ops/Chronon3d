#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::presets::motion {

struct LayerMotion3D {
    bool enabled{false};
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct SoftGlowTextParams {
    std::string text;
    std::string font_path_main{"assets/fonts/Inter-Bold.ttf"};
    std::string font_path_glow{"assets/fonts/Inter-Regular.ttf"};
    LayerMotion3D motion{};

    Vec3 text_pos{0.0f, 0.0f, 0.0f};
    f32 font_size{96.0f};
    f32 outer_size_boost{8.0f};
    f32 inner_size_boost{3.0f};
    f32 outer_blur{26.0f};
    f32 inner_blur{12.0f};
    f32 outer_opacity{0.22f};
    f32 inner_opacity{0.36f};
    f32 tracking{0.0f};
    TextAlign align{TextAlign::Center};
    Color main_color{0.96f, 0.96f, 0.94f, 1.0f};
    Color glow_color{1.0f, 1.0f, 1.0f, 1.0f};
};

inline void apply_layer_motion(LayerBuilder& l, const LayerMotion3D& motion) {
    if (motion.enabled) {
        l.enable_3d();
    }
    l.position(motion.position)
     .rotate(motion.rotation)
     .scale(motion.scale);
}

inline void soft_glow_text(SceneBuilder& s, std::string name, SoftGlowTextParams p) {
    const std::string outer_name = name + "_glow_outer";
    const std::string inner_name = name + "_glow_inner";
    const std::string main_name  = name + "_main";

    auto build_text = [&](const std::string& layer_name,
                          const std::string& text_name,
                          const std::string& font_path,
                          f32 size,
                          f32 opacity,
                          f32 blur,
                          Color color) {
        s.layer(layer_name, [=](LayerBuilder& l) {
            apply_layer_motion(l, p.motion);
            l.opacity(opacity);
            l.text(text_name, {
                .content = p.text,
                .style = {
                    .font_path = font_path,
                    .size = size,
                    .color = color,
                    .align = p.align,
                    .tracking = p.tracking,
                },
                .pos = p.text_pos,
            }).blur(blur);
        });
    };

    build_text(outer_name, "glow_outer", p.font_path_glow, p.font_size + p.outer_size_boost,
               p.outer_opacity, p.outer_blur, p.glow_color);
    build_text(inner_name, "glow_inner", p.font_path_glow, p.font_size + p.inner_size_boost,
               p.inner_opacity, p.inner_blur, p.glow_color);

    s.layer(main_name, [=](LayerBuilder& l) {
        apply_layer_motion(l, p.motion);
        l.text("main", {
            .content = p.text,
            .style = {
                .font_path = p.font_path_main,
                .size = p.font_size,
                .color = p.main_color,
                .align = p.align,
                .tracking = p.tracking,
            },
            .pos = p.text_pos,
        });
    });
}

} // namespace chronon3d::presets::motion

