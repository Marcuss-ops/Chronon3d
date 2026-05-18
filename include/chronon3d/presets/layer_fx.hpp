#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::presets::motion {

using LayerMotion3D = Motion3D;

struct SoftGlowTextParams {
    std::string text;
    std::string font_path_main{"assets/fonts/Inter-Bold.ttf"};
    std::string font_path_glow{"assets/fonts/Inter-Regular.ttf"};
    std::string font_family_main{"Inter"};
    std::string font_family_glow{"Inter"};
    int font_weight_main{800};
    int font_weight_glow{400};
    std::string font_style_main{"normal"};
    std::string font_style_glow{"normal"};
    LayerMotion3D motion{};

    Vec3 text_pos{0.0f, 0.0f, 0.0f};
    f32 font_size{96.0f};
    f32 outer_size_boost{8.0f};
    f32 outer_blur{26.0f};
    f32 outer_opacity{0.22f};
    f32 main_opacity{1.0f};
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
    const std::string main_name  = name + "_main";

    auto build_text = [&](const std::string& layer_name,
                          const std::string& text_name,
                          const std::string& font_path,
                          const std::string& font_family,
                          int font_weight,
                          const std::string& font_style,
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
                    .font_family = font_family,
                    .font_weight = font_weight,
                    .font_style = font_style,
                    .size = size,
                    .color = color,
                    .align = p.align,
                    .tracking = p.tracking,
                },
                .pos = p.text_pos,
            }).blur(blur);
        });
    };

    build_text(outer_name, "glow_outer", p.font_path_glow, p.font_family_glow, p.font_weight_glow, p.font_style_glow,
               p.font_size + p.outer_size_boost,
               p.outer_opacity, p.outer_blur, p.glow_color);

    s.layer(main_name, [=](LayerBuilder& l) {
        apply_layer_motion(l, p.motion);
        l.opacity(p.main_opacity);
        l.text("main", {
            .content = p.text,
            .style = {
                .font_path = p.font_path_main,
                .font_family = p.font_family_main,
                .font_weight = p.font_weight_main,
                .font_style = p.font_style_main,
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
