// ═══════════════════════════════════════════════════════════════════════════
// src/text/word_emphasis_animators.cpp
//
// Canonical semantic emphasis implementation. Lightweight profiles emit one
// animator per selected span; the historical per-letter factory remains an
// explicit expressive option and shares the same parser and animator types.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/word_emphasis_animators.hpp>

#include <string>
#include <utility>

namespace chronon3d::presets::text {

namespace {

constexpr std::string_view kBasePrefix{"base:"};
constexpr std::string_view kNamePrefix{"name:"};
constexpr std::string_view kTitlePrefix{"title:"};
constexpr std::string_view kPhrasePrefix{"phrase:"};
constexpr std::string_view kEmphPrefix{"emph:"};
constexpr std::string_view kWordPrefix{"word:"};

constexpr f32 kSelectorEpsilon = 1e-4f;

constexpr std::string_view kind_token(WordEmphasisKind kind) noexcept {
    switch (kind) {
        case WordEmphasisKind::Base:  return kBasePrefix;
        case WordEmphasisKind::Name:  return kNamePrefix;
        case WordEmphasisKind::Title: return kTitlePrefix;
        case WordEmphasisKind::Emph:  return kEmphPrefix;
        case WordEmphasisKind::None:  return "none:";
    }
    return "none:";
}

std::string kind_slug(WordEmphasisKind kind) {
    const auto token = kind_token(kind);
    return std::string{token.substr(0, token.size() - 1)};
}

void add_opacity_ramp(TextAnimatorSpec& spec,
                      Frame start,
                      Frame duration) {
    OpacityProperty opacity;
    opacity.value.clear();
    opacity.value.add_keyframe(start, 0.0f);
    opacity.value.add_keyframe(
        start + duration,
        1.0f,
        EasingCurve{Easing::OutCubic});
    spec.properties.push_back(std::move(opacity));
}

void add_scale_pop(TextAnimatorSpec& spec,
                   Frame start,
                   Frame peak_at,
                   Frame settle_at,
                   f32 initial,
                   f32 peak) {
    ScaleProperty scale;
    scale.value.clear();
    scale.value.add_keyframe(start, Vec3{initial, initial, 1.0f});
    scale.value.add_keyframe(
        start + peak_at,
        Vec3{peak, peak, 1.0f},
        EasingCurve{Easing::OutBack});
    scale.value.add_keyframe(
        start + settle_at,
        Vec3{1.0f, 1.0f, 1.0f},
        EasingCurve{Easing::InOutSine});
    spec.properties.push_back(std::move(scale));
}

GlyphSelectorSpec full_character_selector(std::string id) {
    GlyphSelectorSpec selector;
    selector.id = std::move(id);
    selector.unit = TextSelectorUnit::Character;
    selector.shape = TextSelectorShape::Square;
    selector.order = TextSelectorOrder::Forward;
    selector.combine = SelectorCombineMode::Replace;
    selector.start = AnimatedValue<f32>(0.0f);
    selector.end = AnimatedValue<f32>(100.0f);
    selector.amount = AnimatedValue<f32>(100.0f);
    selector.exclude_spaces = false;
    return selector;
}

} // namespace

EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id) {
    if (semantic_id.empty()) {
        return {WordEmphasisKind::None, semantic_id};
    }

    const auto match = [semantic_id](std::string_view prefix) {
        return semantic_id.size() >= prefix.size() &&
               semantic_id.compare(0, prefix.size(), prefix) == 0;
    };

    if (match(kBasePrefix)) {
        return {WordEmphasisKind::Base, semantic_id.substr(kBasePrefix.size())};
    }
    if (match(kNamePrefix)) {
        return {WordEmphasisKind::Name, semantic_id.substr(kNamePrefix.size())};
    }
    if (match(kTitlePrefix)) {
        return {WordEmphasisKind::Title, semantic_id.substr(kTitlePrefix.size())};
    }
    if (match(kPhrasePrefix)) {
        return {WordEmphasisKind::Title, semantic_id.substr(kPhrasePrefix.size())};
    }
    if (match(kEmphPrefix)) {
        return {WordEmphasisKind::Emph, semantic_id.substr(kEmphPrefix.size())};
    }
    if (match(kWordPrefix)) {
        return {WordEmphasisKind::Emph, semantic_id.substr(kWordPrefix.size())};
    }
    return {WordEmphasisKind::None, semantic_id};
}

std::optional<TextAnimatorSpec> make_lightweight_emphasis_animator(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    GlyphSelectorSpec selector,
    std::string_view id_suffix) {

    if (kind == WordEmphasisKind::None) {
        return std::nullopt;
    }

    const std::string slug = kind_slug(kind);
    const std::string suffix = id_suffix.empty()
        ? std::string{}
        : "_" + std::string{id_suffix};

    TextAnimatorSpec spec;
    spec.id = "light_emphasis_" + slug + suffix;
    spec.enabled = true;
    spec.transform_mode = TextPropertyBlendMode::Add;
    spec.color_mode = TextPropertyBlendMode::Replace;

    if (selector.id.empty()) {
        selector.id = "sel_light_emphasis_" + slug + suffix;
    }
    spec.selectors.push_back(std::move(selector));

    switch (kind) {
        case WordEmphasisKind::Base: {
            PositionProperty position;
            position.value.clear();
            position.value.add_keyframe(start_frame, Vec3{0.0f, 10.0f, 0.0f});
            position.value.add_keyframe(
                start_frame + Frame{5},
                Vec3{0.0f, 0.0f, 0.0f},
                EasingCurve{Easing::OutCubic});
            spec.properties.push_back(std::move(position));
            add_opacity_ramp(spec, start_frame, Frame{4});
            break;
        }
        case WordEmphasisKind::Title:
            add_scale_pop(spec, start_frame, Frame{4}, Frame{7}, 0.96f, 1.02f);
            add_opacity_ramp(spec, start_frame, Frame{5});
            break;
        case WordEmphasisKind::Name:
            add_scale_pop(spec, start_frame, Frame{4}, Frame{7}, 0.94f, 1.03f);
            add_opacity_ramp(spec, start_frame, Frame{3});
            break;
        case WordEmphasisKind::Emph:
            add_scale_pop(spec, start_frame, Frame{3}, Frame{6}, 0.90f, 1.08f);
            add_opacity_ramp(spec, start_frame, Frame{2});
            break;
        case WordEmphasisKind::None:
            return std::nullopt;
    }

    if (accent.has_value()) {
        FillColorProperty fill;
        fill.color = *accent;
        spec.properties.push_back(fill);
    }

    return spec;
}

std::vector<TextAnimatorSpec> make_word_emphasis_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t letter_count) {

    if (kind == WordEmphasisKind::None || letter_count == 0) {
        return {};
    }

    // Base is deliberately constant-cost. It is the default lightweight
    // entrance and must not create one animator per character on long text.
    if (kind == WordEmphasisKind::Base) {
        auto selector = full_character_selector("sel_base_full_span");
        auto spec = make_lightweight_emphasis_animator(
            kind, accent, start_frame, std::move(selector), "full_span");
        if (!spec.has_value()) {
            return {};
        }
        std::vector<TextAnimatorSpec> result;
        result.reserve(1);
        result.push_back(std::move(*spec));
        return result;
    }

    std::vector<TextAnimatorSpec> specs;
    specs.reserve(letter_count);

    const std::string slug = kind_slug(kind);
    const i64 stagger_step = 2;
    const i64 up_duration = 6;
    const i64 settle_duration = 6;
    const f32 start_scale = 0.7f;
    const f32 peak_scale = 1.15f;
    const f32 end_scale = 1.0f;
    const i64 base_frame = start_frame.integral();
    const f32 per_letter_unit = 100.0f / static_cast<f32>(letter_count);

    for (std::size_t i = 0; i < letter_count; ++i) {
        TextAnimatorSpec spec;
        spec.id = "word_emphasis_" + slug + "_letter_" + std::to_string(i);
        spec.enabled = true;
        spec.transform_mode = TextPropertyBlendMode::Add;
        spec.color_mode = TextPropertyBlendMode::Replace;

        const f32 selector_start = static_cast<f32>(i) * per_letter_unit;
        const f32 selector_end = (i + 1 == letter_count)
            ? 100.0f
            : selector_start + per_letter_unit - kSelectorEpsilon;

        GlyphSelectorSpec selector;
        selector.id = "sel_" + slug + "_letter_" + std::to_string(i);
        selector.unit = TextSelectorUnit::Character;
        selector.shape = TextSelectorShape::Square;
        selector.order = TextSelectorOrder::Forward;
        selector.combine = SelectorCombineMode::Replace;
        selector.start = AnimatedValue<f32>(selector_start);
        selector.end = AnimatedValue<f32>(selector_end);
        selector.amount = AnimatedValue<f32>(100.0f);
        selector.exclude_spaces = false;
        spec.selectors.push_back(std::move(selector));

        const Frame up_start = Frame{
            base_frame + static_cast<i64>(i) * stagger_step};
        const Frame up_end = up_start + Frame{up_duration};
        const Frame settle_end = up_end + Frame{settle_duration};

        ScaleProperty scale;
        scale.value.clear();
        scale.value.add_keyframe(
            up_start, Vec3{start_scale, start_scale, start_scale});
        scale.value.add_keyframe(
            up_end,
            Vec3{peak_scale, peak_scale, peak_scale},
            EasingCurve{Easing::OutBack});
        scale.value.add_keyframe(
            settle_end,
            Vec3{end_scale, end_scale, end_scale},
            EasingCurve{Easing::InOutSine});
        spec.properties.push_back(std::move(scale));

        add_opacity_ramp(spec, up_start, Frame{4});

        if (accent.has_value()) {
            FillColorProperty fill;
            fill.color = *accent;
            spec.properties.push_back(fill);
        }

        specs.push_back(std::move(spec));
    }

    return specs;
}

} // namespace chronon3d::presets::text
