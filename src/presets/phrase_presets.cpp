#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/motion_presets.hpp>

namespace chronon3d::presets::phrase {

namespace {

struct PhraseGroupBuilder {
    const PhraseParams& p;
    std::vector<MotionObject> children;

    PhraseGroupBuilder(const PhraseParams& params) : p(params) {
        children.reserve(4);
    }

    PhraseGroupBuilder& panel(const std::string& id, MotionPreset preset) {
        children.push_back(detail::make_panel(id, p, preset, p.box_size, p.corner_radius));
        return *this;
    }

    PhraseGroupBuilder& accent(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size) {
        children.push_back(detail::make_accent_bar(id, p, preset, pos, size));
        return *this;
    }

    PhraseGroupBuilder& title(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align = TextAlign::Center) {
        children.push_back(detail::make_title_text(id, p, preset, pos, size, align));
        return *this;
    }

    PhraseGroupBuilder& subtitle(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align = TextAlign::Center) {
        if (!p.subtitle.empty()) {
            children.push_back(detail::make_subtitle_text(id, p, preset, pos, size, align));
        }
        return *this;
    }

    PhraseGroupBuilder& quote(const std::string& id, MotionPreset preset, Vec3 pos) {
        children.push_back(detail::make_quote_mark(id, p, preset, pos));
        return *this;
    }

    PhraseGroupBuilder& add(MotionObject obj) {
        children.push_back(std::move(obj));
        return *this;
    }

    MotionObject build(MotionPreset group_preset) {
        return MotionObject::group(p.id, std::move(children))
            .at(p.position)
            .preset(group_preset)
            .time(p.start, p.end);
    }
};

} // namespace

MotionObject make_phrase(PhrasePreset preset, PhraseParams p) {
    PhraseGroupBuilder b(p);

    switch (preset) {
    case PhrasePreset::TitlePop:
        return b.panel("bg", MotionPreset::PopIn)
                .accent("accent", MotionPreset::SlideLeft, {-p.box_size.x * 0.30f, -p.box_size.y * 0.34f, 1.0f}, {p.box_size.x * 0.18f, 10.0f})
                .title("title", MotionPreset::PopGlow, {0.0f, -10.0f, 2.0f}, p.title_size)
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 74.0f, 2.0f}, p.subtitle_size)
                .build(MotionPreset::PopIn);

    case PhrasePreset::LowerThird:
        return b.panel("bg", MotionPreset::SlideUp)
                .accent("accent", MotionPreset::FadeIn, {-p.box_size.x * 0.44f, 0.0f, 1.0f}, {10.0f, p.box_size.y * 0.72f})
                .title("title", MotionPreset::SlideLeft, {-p.box_size.x * 0.28f, -p.box_size.y * 0.10f, 2.0f}, {p.box_size.x * 0.72f, p.title_size.y}, TextAlign::Left)
                .subtitle("subtitle", MotionPreset::FadeIn, {-p.box_size.x * 0.28f, p.box_size.y * 0.20f, 2.0f}, {p.box_size.x * 0.72f, p.subtitle_size.y}, TextAlign::Left)
                .build(MotionPreset::SlideUp);

    case PhrasePreset::CaptionClean:
        return b.add(MotionObject::rect("rule").at({0.0f, p.box_size.y * 0.30f, 0.0f}).size({p.box_size.x * 0.42f, 6.0f}).color(p.accent_color).preset(MotionPreset::FadeIn).time(p.start, p.end))
                .title("caption", MotionPreset::SlideUp, {0.0f, 0.0f, 1.0f}, p.title_size)
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 74.0f, 1.0f}, p.subtitle_size)
                .build(MotionPreset::FadeIn);

    case PhrasePreset::QuoteImpact:
        return b.panel("bg", MotionPreset::PopIn)
                .quote("quote", MotionPreset::FadeIn, {-p.box_size.x * 0.36f, -p.box_size.y * 0.22f, 2.0f})
                .title("quote_text", MotionPreset::PushIn3D, {0.0f, -6.0f, 2.0f}, {p.box_size.x * 0.76f, p.title_size.y})
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 84.0f, 2.0f}, p.subtitle_size)
                .build(MotionPreset::PopIn);

    case PhrasePreset::WarningPulse:
        return b.panel("bg", MotionPreset::PopGlow)
                .accent("accent", MotionPreset::PopGlow, {-p.box_size.x * 0.45f, 0.0f, 1.0f}, {12.0f, p.box_size.y * 0.76f})
                .title("warning", MotionPreset::FadeIn, {0.0f, -8.0f, 2.0f}, p.title_size)
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 84.0f, 2.0f}, p.subtitle_size)
                .build(MotionPreset::ShakeImpact);

    case PhrasePreset::TypewriterCaption: {
        auto title = detail::make_title_text("title", p, MotionPreset::None, {0.0f, -10.0f, 2.0f}, p.title_size);
        motion::perspective_sweep_text_reveal(title);
        return b.panel("bg", MotionPreset::FadeIn)
                .add(std::move(title))
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 74.0f, 2.0f}, p.subtitle_size)
                .build(MotionPreset::FadeIn);
    }

    case PhrasePreset::BounceTitle:
        return b.panel("bg", MotionPreset::KineticBounce)
                .accent("accent", MotionPreset::FadeIn, {-p.box_size.x * 0.34f, -p.box_size.y * 0.28f, 1.0f}, {p.box_size.x * 0.22f, 10.0f})
                .title("title", MotionPreset::KineticBounce, {0.0f, -8.0f, 2.0f}, p.title_size)
                .build(MotionPreset::KineticBounce);

    case PhrasePreset::GlitchBanner:
        return b.panel("bg", MotionPreset::GlitchIn)
                .accent("accent", MotionPreset::GlitchIn, {-p.box_size.x * 0.46f, 0.0f, 1.0f}, {12.0f, p.box_size.y * 0.80f})
                .title("title", MotionPreset::GlitchIn, {0.0f, -10.0f, 2.0f}, p.title_size)
                .subtitle("subtitle", MotionPreset::FadeIn, {0.0f, 82.0f, 2.0f}, p.subtitle_size)
                .build(MotionPreset::GlitchIn);
    }

    return MotionObject::group(p.id, {});
}

} // namespace chronon3d::presets::phrase
