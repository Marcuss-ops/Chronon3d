#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/presets/style_kit.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chronon3d::presets::phrase {

using motion::MotionObject;
using motion::MotionPreset;
using motion::draw_motion_object;
using motion::TextAlign;

enum class PhrasePreset {
    TitlePop,
    LowerThird,
    CaptionClean,
    QuoteImpact,
    WarningPulse,
    TypewriterCaption,
    BounceTitle,
    GlitchBanner,
};

struct PhraseTheme {
    Color panel{0.05f, 0.06f, 0.08f, 0.86f};
    Color accent{0.98f, 0.54f, 0.16f, 1.0f};
    Color title{1.0f, 1.0f, 1.0f, 1.0f};
    Color subtitle{0.76f, 0.79f, 0.84f, 1.0f};
    f32 radius{30.0f};
    std::string font_family{"Inter"};

    PhraseTheme() = default;

    PhraseTheme(const StyleKit& kit) {
        panel = kit.colors.background;
        accent = kit.colors.accent;
        title = kit.colors.primary;
        subtitle = kit.colors.secondary;
        radius = kit.corner_radius;
        font_family = kit.typography.font_family;
    }

    static PhraseTheme documentary() {
        return PhraseTheme(StyleKit::documentary());
    }

    static PhraseTheme tech() {
        return PhraseTheme(StyleKit::tech());
    }

    static PhraseTheme luxury() {
        return PhraseTheme(StyleKit::luxury());
    }

    static PhraseTheme warning() {
        return PhraseTheme(StyleKit::warning());
    }

    static PhraseTheme sports() {
        return PhraseTheme(StyleKit::sports());
    }

    static PhraseTheme gossip() {
        return PhraseTheme(StyleKit::gossip());
    }

    static PhraseTheme breaking_news() {
        return PhraseTheme(StyleKit::breaking_news());
    }
};

struct PhraseParams {
    std::string id{"phrase"};
    std::string text;
    std::string subtitle;
    Frame start{0};
    Frame end{90};

    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec2 box_size{960.0f, 220.0f};
    Vec2 title_size{860.0f, 120.0f};
    Vec2 subtitle_size{860.0f, 52.0f};

    std::string title_font_path{"assets/fonts/Inter-Bold.ttf"};
    std::string subtitle_font_path{"assets/fonts/Inter-Regular.ttf"};
    f32 title_font_size{72.0f};
    f32 subtitle_font_size{28.0f};

    Color panel_color{0.05f, 0.06f, 0.08f, 0.86f};
    Color accent_color{0.98f, 0.54f, 0.16f, 1.0f};
    Color text_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color subtitle_color{0.76f, 0.79f, 0.84f, 1.0f};
    f32 corner_radius{30.0f};

    PhraseParams& apply_theme(const PhraseTheme& theme) {
        panel_color = theme.panel;
        accent_color = theme.accent;
        text_color = theme.title;
        subtitle_color = theme.subtitle;
        corner_radius = theme.radius;
        return *this;
    }
};

namespace detail {

inline MotionObject make_panel(
    std::string id,
    const PhraseParams& p,
    MotionPreset preset,
    Vec2 size,
    f32 radius = 28.0f
) {
    return MotionObject::rounded_rect(std::move(id))
        .at({0.0f, 0.0f, 0.0f})
        .size(size)
        .radius(radius)
        .color(p.panel_color)
        .preset(preset)
        .time(p.start, p.end);
}

inline MotionObject make_accent_bar(
    std::string id,
    const PhraseParams& p,
    MotionPreset preset,
    Vec3 pos,
    Vec2 size
) {
    return MotionObject::rect(std::move(id))
        .at(pos)
        .size(size)
        .color(p.accent_color)
        .preset(preset)
        .time(p.start, p.end);
}

inline MotionObject make_title_text(
    std::string id,
    const PhraseParams& p,
    MotionPreset preset,
    Vec3 pos,
    Vec2 size,
    TextAlign align = TextAlign::Center
) {
    return MotionObject::text(std::move(id), p.text)
        .at(pos)
        .size(size)
        .font_path(p.title_font_path)
        .font_size(p.title_font_size)
        .tracking(0.2f)
        .align(align)
        .color(p.text_color)
        .preset(preset)
        .glow(preset == MotionPreset::PopGlow || preset == MotionPreset::ShakeImpact)
        .time(p.start, p.end);
}

inline MotionObject make_subtitle_text(
    std::string id,
    const PhraseParams& p,
    MotionPreset preset,
    Vec3 pos,
    Vec2 size,
    TextAlign align = TextAlign::Center
) {
    return MotionObject::text(std::move(id), p.subtitle)
        .at(pos)
        .size(size)
        .font_path(p.subtitle_font_path)
        .font_size(p.subtitle_font_size)
        .tracking(0.15f)
        .align(align)
        .color(p.subtitle_color)
        .preset(preset)
        .time(p.start, p.end);
}

inline MotionObject make_quote_mark(
    std::string id,
    const PhraseParams& p,
    MotionPreset preset,
    Vec3 pos
) {
    return MotionObject::text(std::move(id), "\"")
        .at(pos)
        .size({120.0f, 120.0f})
        .font_path(p.title_font_path)
        .font_size(110.0f)
        .align(TextAlign::Center)
        .color(p.accent_color)
        .preset(preset)
        .glow(true)
        .time(p.start, p.end);
}

} // namespace detail

MotionObject make_phrase(PhrasePreset preset, PhraseParams p);

inline void draw_phrase(SceneBuilder& s, const FrameContext& ctx, PhrasePreset preset, PhraseParams p) {
    draw_motion_object(s, ctx, make_phrase(preset, std::move(p)));
}

} // namespace chronon3d::presets::phrase
