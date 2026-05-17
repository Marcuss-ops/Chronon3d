#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/shape.hpp>
#include <string>

namespace chronon3d::presets {

struct TextBoxStyle {
    TextStyle text_style{};

    Vec2 size{600.0f, 100.0f};
    f32  padding_x{24.0f};
    f32  padding_y{14.0f};

    bool  background_enabled{true};
    Color background_color{0.0f, 0.0f, 0.0f, 0.7f};
    f32   background_radius{14.0f};

    bool  shadow_enabled{true};
    Vec2  shadow_offset{0.0f, 6.0f};
    Color shadow_color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   shadow_blur{18.0f};
};

struct TextBoxParams {
    std::string   text;
    Vec3          position{0.0f, 0.0f, 0.0f};
    TextBoxStyle  style{};
};

// Add a text box (background + text) to an existing LayerBuilder.
// The background and text are added as named RenderNodes inside the layer.
inline void text_box(LayerBuilder& l, const std::string& node_prefix, const TextBoxParams& p) {
    const auto& st = p.style;

    if (st.background_enabled) {
        l.rounded_rect(node_prefix + ":bg", {
            .size   = st.size,
            .radius = st.background_radius,
            .color  = st.background_color,
            .pos    = p.position,
        });
        if (st.shadow_enabled) {
            l.drop_shadow(st.shadow_offset, st.shadow_color, st.shadow_blur);
        }
    }

    TextBox box;
    box.size    = {st.size.x - st.padding_x * 2.0f, st.size.y - st.padding_y * 2.0f};
    box.enabled = true;

    TextStyle ts  = st.text_style;
    ts.align      = TextAlign::Center;
    ts.auto_scale = true;

    l.text(node_prefix + ":text", {
        .content = p.text,
        .style   = ts,
        .pos     = p.position,
        .box     = box,
    });
}

// Add a text box as a standalone layer in a SceneBuilder.
inline void text_box_layer(SceneBuilder& s, const std::string& layer_name,
                            const TextBoxParams& p) {
    s.layer(layer_name, [&p](LayerBuilder& l) {
        l.position(p.position);
        TextBoxParams local = p;
        local.position = {0, 0, 0};
        text_box(l, "tb", local);
    });
}

} // namespace chronon3d::presets
