// ── Typewriter Build ───────────────────────────────────────────────────────────
// Implementation of typewriter_build and typewriter_text.
//
// Provenance: restored to core from the externalized content pack
// (content/text/typewriter_build.cpp, removed in 6e6905116).

#include <chronon3d/text/typewriter_layout.hpp>
#include <chronon3d/backends/text/text_unicode_utils.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_atlas.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace chronon3d::content::text {

using chronon3d::TextError;
using chronon3d::TextErrorCode;
using chronon3d::Result;
using chronon3d::Anchor;

TextDefinition typewriter_text(TextDefinition o,
    Frame frame,
    f32 chars_per_frame,
    TypewriterOptions tw)
{
    using chronon3d::detail::grapheme_cluster_count;
    using chronon3d::detail::grapheme_byte_offset_at;

    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(tw.start_delay);
    const f32 total_chars_f = static_cast<f32>(grapheme_cluster_count(o.content.value));

    auto make_base = [&](std::string value, Color c) -> TextDefinition {
        return TextDefinition{
            .content = {.value = std::move(value)},
            .style = {
                .font = {.font_path   = o.style.font.font_path,
                         .font_family = o.style.font.font_family,
                         .font_weight = o.style.font.font_weight,
                         .font_style  = o.style.font.font_style,
                         .font_size   = o.style.font.font_size},
                .color = c
            },
            .frame = {
                .size = o.frame.size,
                .placement = o.frame.placement,
                .anchor = o.frame.anchor,
                .align = o.frame.align,
                .vertical_align = o.frame.vertical_align,
                .wrap = o.frame.wrap,
                .overflow = o.frame.overflow,
                .centering_mode = o.frame.centering_mode,
                .line_height = o.frame.line_height,
                .tracking = o.frame.tracking,
                .auto_fit = o.frame.auto_fit,
                .min_font_size = o.frame.min_font_size,
                .max_font_size = o.frame.max_font_size,
                .max_lines = o.frame.max_lines
            }
        };
    };

    if (raw_frame < 0.0f || total_chars_f <= 0.0f) {
        Color c = o.style.color;
        c.a = 0.0f;
        return make_base(std::string(" "), c);
    }

    const f32 linear_t = std::clamp(raw_frame * chars_per_frame / total_chars_f,
                                    0.0f, 1.0f);
    const f32 eased_t  = tw.easing.apply(linear_t);

    if (eased_t >= 1.0f) {
        return make_base(std::move(o.content.value), o.style.color);
    }

    const size_t revealed = static_cast<size_t>(eased_t * total_chars_f);
    const f32    partial  = (eased_t * total_chars_f) - static_cast<f32>(revealed);

    std::string visible = (revealed == 0)
        ? std::string(" ")
        : o.content.value.substr(0, grapheme_byte_offset_at(o.content.value, revealed));

    Color c = o.style.color;
    if (tw.fade_chars > 0.0f && revealed < static_cast<size_t>(total_chars_f) && revealed > 0) {
        const f32 fade_range = std::clamp(tw.fade_chars, 0.0f, 1.0f);
        const f32 fade_t = std::clamp(1.0f - fade_range * partial, 0.25f, 1.0f);
        c.a *= fade_t;
    }

    return make_base(std::move(visible), c);
}

Result<bool, TextError> typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame,
    FontEngine& engine)
{
    // The complete source string is shaped and laid out by Chronon3D's
    // canonical text pipeline.  In particular, do not compile one mini text
    // run/layer per character here: that loses the authoritative advances and
    // makes the final frame contain both the full run and glyph copies.
    (void)frame;
    using chronon3d::detail::grapheme_cluster_count;

    if (opts.text.empty()) return make_text_error(
        TextErrorCode::EmptyText,
        "typewriter_build: text is empty");

    const std::size_t cluster_count = grapheme_cluster_count(opts.text);
    if (cluster_count == 0) return make_text_error(
        TextErrorCode::NoLayoutChars,
        "typewriter_build: text has zero grapheme clusters");

    const f32 chars_per_frame = std::max(opts.chars_per_frame, 0.0001f);
    const int reveal_frames = std::max(
        1, static_cast<int>(std::ceil(static_cast<f32>(cluster_count) / chars_per_frame)));
    const Frame reveal_start = opts.start_delay;
    const Frame reveal_end = reveal_start + Frame{reveal_frames};

    GlyphSelectorSpec hidden_clusters;
    hidden_clusters.id = std::string(layer_prefix) + "_hidden_clusters";
    hidden_clusters.unit = TextSelectorUnit::Grapheme;
    // Select the unrevealed suffix as a hard range.  RampDown would leave
    // the whole run partially visible at frame zero and then hide it again
    // as the boundary advances; Square gives the required hidden-to-visible
    // typewriter direction.
    hidden_clusters.shape = TextSelectorShape::Square;
    hidden_clusters.order = TextSelectorOrder::Forward;
    hidden_clusters.start = 0.0f;
    hidden_clusters.start.add_keyframe(reveal_start, 0.0f);
    hidden_clusters.start.add_keyframe(reveal_end, 100.0f, opts.easing);
    hidden_clusters.end = 100.0f;
    hidden_clusters.exclude_spaces = true;

    TextAnimatorSpec reveal;
    reveal.id = std::string(layer_prefix) + "_typewriter_reveal";
    reveal.selectors.push_back(std::move(hidden_clusters));
    reveal.properties.push_back(OpacityProperty{0.0f});
    reveal.transform_mode = TextPropertyBlendMode::Multiply;

    TextDefinition definition;
    definition.content.value = opts.text;
    definition.style.font.font_path = opts.font_asset;
    definition.style.font.font_family = opts.font_family;
    definition.style.font.font_weight = opts.font_weight;
    definition.style.font.font_size = opts.font_size;
    definition.style.color = opts.color;
    definition.frame.size = opts.box;
    definition.frame.placement =
        TextPlacement{TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
    definition.frame.anchor = TextAnchor::Center;
    definition.frame.align = TextAlign::Center;
    definition.frame.vertical_align = VerticalAlign::Middle;
    definition.frame.wrap = TextWrap::None;
    definition.frame.overflow = TextOverflow::Clip;
    definition.frame.centering_mode = TextCenteringMode::PixelInk;
    definition.frame.line_height = opts.line_height;
    definition.frame.tracking = opts.tracking;
    definition.animation.animators.push_back(std::move(reveal));
    definition.animation.start_delay = opts.start_delay;
    definition.animation.duration = reveal_end;
    definition.animation.cache_layout = true;

    s.font_engine(&engine);
    const std::string layer_name{layer_prefix};
    s.layer(layer_name, [definition = std::move(definition)](LayerBuilder& l) mutable {
        l.text("text", std::move(definition));
    });
    return true;
}

} // namespace chronon3d::content::text
