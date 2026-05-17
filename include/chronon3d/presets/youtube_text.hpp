#pragma once

#include <chronon3d/presets/text_box.hpp>
#include <string>

namespace chronon3d::presets::youtube {

// ── Style presets ─────────────────────────────────────────────

inline TextBoxStyle title_style(const std::string& font_path = "") {
    TextBoxStyle s;
    s.size              = {900.0f, 120.0f};
    s.background_color  = {0.05f, 0.05f, 0.05f, 0.85f};
    s.background_radius = 20.0f;
    s.padding_x         = 36.0f;
    s.padding_y         = 20.0f;
    s.text_style.size   = 64.0f;
    s.text_style.color  = {1.0f, 1.0f, 1.0f, 1.0f};
    s.text_style.align  = TextAlign::Center;
    if (!font_path.empty()) s.text_style.font_path = font_path;
    return s;
}

inline TextBoxStyle subtitle_style(const std::string& font_path = "") {
    TextBoxStyle s;
    s.size              = {800.0f, 80.0f};
    s.background_color  = {0.0f, 0.0f, 0.0f, 0.65f};
    s.background_radius = 12.0f;
    s.padding_x         = 28.0f;
    s.padding_y         = 12.0f;
    s.text_style.size   = 36.0f;
    s.text_style.color  = {1.0f, 1.0f, 1.0f, 0.95f};
    s.text_style.align  = TextAlign::Center;
    if (!font_path.empty()) s.text_style.font_path = font_path;
    return s;
}

inline TextBoxStyle lower_third_style(const std::string& font_path = "") {
    TextBoxStyle s;
    s.size              = {700.0f, 90.0f};
    s.background_color  = {0.10f, 0.10f, 0.90f, 0.90f};
    s.background_radius = 8.0f;
    s.padding_x         = 32.0f;
    s.padding_y         = 16.0f;
    s.text_style.size   = 40.0f;
    s.text_style.color  = {1.0f, 1.0f, 1.0f, 1.0f};
    s.text_style.align  = TextAlign::Left;
    if (!font_path.empty()) s.text_style.font_path = font_path;
    return s;
}

inline TextBoxStyle quote_style(const std::string& font_path = "") {
    TextBoxStyle s;
    s.size              = {860.0f, 160.0f};
    s.background_color  = {1.0f, 1.0f, 1.0f, 0.10f};
    s.background_radius = 24.0f;
    s.padding_x         = 48.0f;
    s.padding_y         = 32.0f;
    s.shadow_enabled    = false;
    s.text_style.size   = 38.0f;
    s.text_style.color  = {1.0f, 1.0f, 1.0f, 1.0f};
    s.text_style.align  = TextAlign::Center;
    s.text_style.line_height = 1.4f;
    if (!font_path.empty()) s.text_style.font_path = font_path;
    return s;
}

// ── Layer helpers ─────────────────────────────────────────────

inline void title_card(LayerBuilder& l, const std::string& text,
                        const std::string& font_path = "") {
    text_box(l, "title", {.text = text, .style = title_style(font_path)});
}

inline void subtitle_box(LayerBuilder& l, const std::string& text,
                          const std::string& font_path = "") {
    text_box(l, "subtitle", {.text = text, .style = subtitle_style(font_path)});
}

inline void lower_third(LayerBuilder& l, const std::string& title,
                         const std::string& font_path = "") {
    text_box(l, "lower3rd", {.text = title, .style = lower_third_style(font_path)});
}

inline void quote_box(LayerBuilder& l, const std::string& quote,
                       const std::string& font_path = "") {
    text_box(l, "quote", {.text = quote, .style = quote_style(font_path)});
}

} // namespace chronon3d::presets::youtube
