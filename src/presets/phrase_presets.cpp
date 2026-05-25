#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/motion_presets.hpp>

namespace chronon3d::presets::phrase {

MotionObject make_phrase(PhrasePreset preset, PhraseParams p) {
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
            auto title = detail::make_title_text(
                "title",
                p,
                MotionPreset::None,
                {0.0f, -10.0f, 2.0f},
                p.title_size,
                TextAlign::Center
            );
            motion::perspective_sweep_text_reveal(title);
            children.push_back(std::move(title));
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
            .preset(MotionPreset::FadeIn)
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

} // namespace chronon3d::presets::phrase
