#include <chronon3d/presets/text/word_emphasis_animators.hpp>

#include <iterator>
#include <string>

namespace chronon3d::presets::text {
namespace {

constexpr std::string_view kBase{"base:"};
constexpr std::string_view kPhrase{"phrase:"};
constexpr std::string_view kName{"name:"};
constexpr std::string_view kWord{"word:"};
constexpr std::string_view kTitle{"title:"};
constexpr std::string_view kEmph{"emph:"};

constexpr std::string_view token(WordEmphasisKind kind) noexcept {
    switch (kind) {
        case WordEmphasisKind::Base:   return kBase;
        case WordEmphasisKind::Phrase: return kPhrase;
        case WordEmphasisKind::Name:   return kName;
        case WordEmphasisKind::Word:   return kWord;
        case WordEmphasisKind::Title:  return kTitle;
        case WordEmphasisKind::Emph:   return kEmph;
        case WordEmphasisKind::None:   return "none:";
    }
    return "none:";
}

[[nodiscard]] WordEmphasisKind canonical_kind(WordEmphasisKind kind) noexcept {
    if (kind == WordEmphasisKind::Title) return WordEmphasisKind::Phrase;
    if (kind == WordEmphasisKind::Emph) return WordEmphasisKind::Word;
    return kind;
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id) {
    if (starts_with(semantic_id, kBase))   return {WordEmphasisKind::Base, semantic_id.substr(kBase.size())};
    if (starts_with(semantic_id, kPhrase)) return {WordEmphasisKind::Phrase, semantic_id.substr(kPhrase.size())};
    if (starts_with(semantic_id, kName))   return {WordEmphasisKind::Name, semantic_id.substr(kName.size())};
    if (starts_with(semantic_id, kWord))   return {WordEmphasisKind::Word, semantic_id.substr(kWord.size())};
    if (starts_with(semantic_id, kTitle))  return {WordEmphasisKind::Title, semantic_id.substr(kTitle.size())};
    if (starts_with(semantic_id, kEmph))   return {WordEmphasisKind::Emph, semantic_id.substr(kEmph.size())};
    return {WordEmphasisKind::None, semantic_id};
}

std::vector<TextAnimatorSpec> make_light_text_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t span_index,
    std::size_t span_count) {
    if (span_count == 0 || span_index >= span_count) return {};
    if (kind == WordEmphasisKind::None) kind = WordEmphasisKind::Base;

    TextAnimatorSpec spec;
    spec.id = "text_v1_" + std::string(token(kind).substr(0, token(kind).size() - 1))
        + "_span_" + std::to_string(span_index);
    spec.enabled = true;

    const f32 span_width = 100.0f / static_cast<f32>(span_count);
    GlyphSelectorSpec selector;
    selector.id = spec.id + "_selector";
    selector.unit = TextSelectorUnit::Word;
    selector.shape = TextSelectorShape::Square;
    selector.start = AnimatedValue<f32>{static_cast<f32>(span_index) * span_width};
    selector.end = AnimatedValue<f32>{static_cast<f32>(span_index + 1) * span_width};
    selector.amount = AnimatedValue<f32>{100.0f};
    selector.exclude_spaces = true;
    spec.selectors.push_back(std::move(selector));

    const Frame f0 = start_frame;
    const Frame f1 = f0 + Frame{2};
    const Frame f2 = f0 + Frame{6};

    auto add_scale = [&](f32 from, f32 peak) {
        ScaleProperty property;
        property.value.clear();
        property.value.add_keyframe(f0, Vec3{from, from, 1.0f});
        property.value.add_keyframe(f1, Vec3{peak, peak, 1.0f}, EasingCurve{Easing::OutBack});
        property.value.add_keyframe(f2, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::InOutSine});
        spec.properties.push_back(std::move(property));
    };
    auto add_opacity = [&](Frame end) {
        OpacityProperty property;
        property.value.clear();
        property.value.add_keyframe(f0, 0.0f);
        property.value.add_keyframe(end, 1.0f, EasingCurve{Easing::OutCubic});
        spec.properties.push_back(std::move(property));
    };

    switch (kind) {
        case WordEmphasisKind::Base: {
            PositionProperty property;
            property.value.clear();
            property.value.add_keyframe(f0, Vec3{0.0f, 10.0f, 0.0f});
            property.value.add_keyframe(f2, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            spec.properties.push_back(std::move(property));
            add_opacity(f2);
            break;
        }
        case WordEmphasisKind::Phrase:
        case WordEmphasisKind::Title:
            add_scale(0.96f, 1.02f);
            add_opacity(f2);
            break;
        case WordEmphasisKind::Name:
            add_scale(0.94f, 1.04f);
            add_opacity(f2);
            if (accent) spec.properties.push_back(FillColorProperty{*accent});
            break;
        case WordEmphasisKind::Word:
        case WordEmphasisKind::Emph:
            add_scale(0.90f, 1.08f);
            add_opacity(f1);
            if (accent) spec.properties.push_back(FillColorProperty{*accent});
            break;
        case WordEmphasisKind::None:
            break;
    }
    return {std::move(spec)};
}

std::vector<TextAnimatorSpec> make_light_text_animators_for_semantics(
    std::span<const std::string_view> semantic_ids,
    std::optional<Color> accent,
    Frame start_frame) {
    struct Group { WordEmphasisKind kind; std::size_t first; };
    std::vector<Group> groups;
    groups.reserve(semantic_ids.size());

    WordEmphasisKind previous = WordEmphasisKind::None;
    for (std::size_t i = 0; i < semantic_ids.size(); ++i) {
        const WordEmphasisKind parsed = canonical_kind(parse_emphasis_prefix(semantic_ids[i]).kind);
        if (parsed == WordEmphasisKind::None) {
            previous = WordEmphasisKind::None;
            continue;
        }
        if (parsed != previous) groups.push_back(Group{parsed, i});
        previous = parsed;
    }
    if (groups.empty()) return {};

    std::vector<TextAnimatorSpec> result;
    result.reserve(groups.size());
    for (std::size_t i = 0; i < groups.size(); ++i) {
        auto one = make_light_text_animators(
            groups[i].kind, accent, start_frame, i, groups.size());
        result.insert(result.end(),
                      std::make_move_iterator(one.begin()),
                      std::make_move_iterator(one.end()));
    }
    return result;
}

} // namespace chronon3d::presets::text
