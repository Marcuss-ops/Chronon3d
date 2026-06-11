#include <chronon3d/presets/phrase/phrase_group_builder.hpp>

namespace chronon3d::presets::phrase {

PhraseGroupBuilder::PhraseGroupBuilder(const PhraseParams& params) : p(params) {
    children.reserve(4);
}

PhraseGroupBuilder& PhraseGroupBuilder::panel(const std::string& id, MotionPreset preset) {
    children.push_back(detail::make_panel(id, p, preset, p.box_size, p.corner_radius));
    return *this;
}

PhraseGroupBuilder& PhraseGroupBuilder::accent(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size) {
    children.push_back(detail::make_accent_bar(id, p, preset, pos, size));
    return *this;
}

PhraseGroupBuilder& PhraseGroupBuilder::title(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align) {
    children.push_back(detail::make_title_text(id, p, preset, pos, size, align));
    return *this;
}

PhraseGroupBuilder& PhraseGroupBuilder::subtitle(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align) {
    if (!p.subtitle.empty()) {
        children.push_back(detail::make_subtitle_text(id, p, preset, pos, size, align));
    }
    return *this;
}

PhraseGroupBuilder& PhraseGroupBuilder::quote(const std::string& id, MotionPreset preset, Vec3 pos) {
    children.push_back(detail::make_quote_mark(id, p, preset, pos));
    return *this;
}

PhraseGroupBuilder& PhraseGroupBuilder::add(MotionObject obj) {
    children.push_back(std::move(obj));
    return *this;
}

MotionObject PhraseGroupBuilder::build(MotionPreset group_preset) {
    return MotionObject::group(p.id, std::move(children))
        .at(p.position)
        .preset(group_preset)
        .time(p.start, p.end);
}

} // namespace chronon3d::presets::phrase
