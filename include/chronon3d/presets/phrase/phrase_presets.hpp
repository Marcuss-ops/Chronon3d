#pragma once

#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

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

inline MotionObject make_phrase(PhrasePreset preset, PhraseParams p) {
    switch (preset) {
    case PhrasePreset::TitlePop:
        {
            std::vector<MotionObject> children;
            children.reserve(4);
            children.push_back(detail::make_panel("bg", p, MotionPreset::PopIn, p.box_size, p.corner_radius));
            children.push_back(detail::make_accent_bar(
                "accent",
                p,
                MotionPreset::SlideLeft,
                {-p.box_size.x * 0.30f, -p.box_size.y * 0.34f, 1.0f},
                {p.box_size.x * 0.18f, 10.0f}
            ));
            children.push_back(detail::make_title_text(
                "title",
                p,
                MotionPreset::PopGlow,
                {0.0f, -10.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 74.0f, 2.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
            .at(p.position)
            .preset(MotionPreset::PopIn)
            .time(p.start, p.end);
        }

    case PhrasePreset::LowerThird:
        {
            std::vector<MotionObject> children;
            children.reserve(4);
            children.push_back(detail::make_panel("bg", p, MotionPreset::SlideUp, p.box_size, p.corner_radius));
            children.push_back(detail::make_accent_bar(
                "accent",
                p,
                MotionPreset::FadeIn,
                {-p.box_size.x * 0.44f, 0.0f, 1.0f},
                {10.0f, p.box_size.y * 0.72f}
            ));
            children.push_back(detail::make_title_text(
                "title",
                p,
                MotionPreset::SlideLeft,
                {-p.box_size.x * 0.28f, -p.box_size.y * 0.10f, 2.0f},
                {p.box_size.x * 0.72f, p.title_size.y},
                TextAlign::Left
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {-p.box_size.x * 0.28f, p.box_size.y * 0.20f, 2.0f},
                    {p.box_size.x * 0.72f, p.subtitle_size.y},
                    TextAlign::Left
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
            .at(p.position)
            .preset(MotionPreset::SlideUp)
            .time(p.start, p.end);
        }

    case PhrasePreset::CaptionClean:
        {
            std::vector<MotionObject> children;
            children.reserve(3);
            children.push_back(MotionObject::rect("rule")
                .at({0.0f, p.box_size.y * 0.30f, 0.0f})
                .size({p.box_size.x * 0.42f, 6.0f})
                .color(p.accent_color)
                .preset(MotionPreset::FadeIn)
                .time(p.start, p.end));
            children.push_back(detail::make_title_text(
                "caption",
                p,
                MotionPreset::SlideUp,
                {0.0f, 0.0f, 1.0f},
                p.title_size,
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 74.0f, 1.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
            .at(p.position)
            .preset(MotionPreset::FadeIn)
            .time(p.start, p.end);
        }

    case PhrasePreset::QuoteImpact:
        {
            std::vector<MotionObject> children;
            children.reserve(4);
            children.push_back(detail::make_panel("bg", p, MotionPreset::PopIn, p.box_size, p.corner_radius));
            children.push_back(detail::make_quote_mark("quote", p, MotionPreset::FadeIn, {-p.box_size.x * 0.36f, -p.box_size.y * 0.22f, 2.0f}));
            children.push_back(detail::make_title_text(
                "quote_text",
                p,
                MotionPreset::PushIn3D,
                {0.0f, -6.0f, 2.0f},
                {p.box_size.x * 0.76f, p.title_size.y},
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 84.0f, 2.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
            .at(p.position)
            .preset(MotionPreset::PopIn)
            .time(p.start, p.end);
        }

    case PhrasePreset::WarningPulse:
        {
            std::vector<MotionObject> children;
            children.reserve(4);
            children.push_back(detail::make_panel("bg", p, MotionPreset::PopGlow, p.box_size, p.corner_radius));
            children.push_back(detail::make_accent_bar(
                "accent",
                p,
                MotionPreset::PopGlow,
                {-p.box_size.x * 0.45f, 0.0f, 1.0f},
                {12.0f, p.box_size.y * 0.76f}
            ));
            children.push_back(detail::make_title_text(
                "warning",
                p,
                MotionPreset::FadeIn,
                {0.0f, -8.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 84.0f, 2.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
            .at(p.position)
            .preset(MotionPreset::ShakeImpact)
            .time(p.start, p.end);
        }

    case PhrasePreset::TypewriterCaption:
        {
            std::vector<MotionObject> children;
            children.reserve(3);
            children.push_back(detail::make_panel("bg", p, MotionPreset::FadeIn, p.box_size, p.corner_radius));
            children.push_back(detail::make_title_text(
                "title",
                p,
                MotionPreset::TypewriterReveal,
                {0.0f, -10.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 74.0f, 2.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
                .at(p.position)
                .preset(MotionPreset::TypewriterReveal)
                .time(p.start, p.end);
        }

    case PhrasePreset::BounceTitle:
        {
            std::vector<MotionObject> children;
            children.reserve(3);
            children.push_back(detail::make_panel("bg", p, MotionPreset::KineticBounce, p.box_size, p.corner_radius));
            children.push_back(detail::make_accent_bar(
                "accent",
                p,
                MotionPreset::FadeIn,
                {-p.box_size.x * 0.34f, -p.box_size.y * 0.28f, 1.0f},
                {p.box_size.x * 0.22f, 10.0f}
            ));
            children.push_back(detail::make_title_text(
                "title",
                p,
                MotionPreset::KineticBounce,
                {0.0f, -8.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            ));
            return MotionObject::group(std::move(p.id), std::move(children))
                .at(p.position)
                .preset(MotionPreset::KineticBounce)
                .time(p.start, p.end);
        }

    case PhrasePreset::GlitchBanner:
        {
            std::vector<MotionObject> children;
            children.reserve(4);
            children.push_back(detail::make_panel("bg", p, MotionPreset::GlitchIn, p.box_size, p.corner_radius));
            children.push_back(detail::make_accent_bar(
                "accent",
                p,
                MotionPreset::GlitchIn,
                {-p.box_size.x * 0.46f, 0.0f, 1.0f},
                {12.0f, p.box_size.y * 0.80f}
            ));
            children.push_back(detail::make_title_text(
                "title",
                p,
                MotionPreset::GlitchIn,
                {0.0f, -10.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            ));
            if (!p.subtitle.empty()) {
                children.push_back(detail::make_subtitle_text(
                    "subtitle",
                    p,
                    MotionPreset::FadeIn,
                    {0.0f, 82.0f, 2.0f},
                    p.subtitle_size,
                    TextAlign::Center
                ));
            }
            return MotionObject::group(std::move(p.id), std::move(children))
                .at(p.position)
                .preset(MotionPreset::GlitchIn)
                .time(p.start, p.end);
        }
    }

    return MotionObject::group(std::move(p.id), {});
}

inline void draw_phrase(SceneBuilder& s, const FrameContext& ctx, PhrasePreset preset, PhraseParams p) {
    draw_motion_object(s, ctx, make_phrase(preset, std::move(p)));
}

} // namespace chronon3d::presets::phrase
