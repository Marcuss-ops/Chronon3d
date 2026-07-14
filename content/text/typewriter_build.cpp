// ── Typewriter Build ───────────────────────────────────────────────────────────
// Implementation of typewriter_build and typewriter_text.
// Extracted from text_helpers_typewriter.hpp.

#include "text_helpers_typewriter.hpp"
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

TextDefinition typewriter_text(CenterTextOptions o,
    Frame frame,
    f32 chars_per_frame,
    TypewriterOptions tw)
{
    using chronon3d::detail::grapheme_cluster_count;
    using chronon3d::detail::grapheme_byte_offset_at;

    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(tw.start_delay);
    const f32 total_chars_f = static_cast<f32>(grapheme_cluster_count(o.text));

    auto make_base = [&](std::string value, Color c) -> TextDefinition {
        return TextDefinition{
            .content = {.value = std::move(value)},
            .style = {
                .font = {.font_path   = std::move(o.font_asset),
                         .font_family = std::move(o.font_family),
                         .font_weight = o.font_weight,
                         .font_style  = std::move(o.font_style),
                         .font_size   = o.font_size},
                .color = c
            },
            .frame = {
                .size = o.box,
                .placement = TextPlacement{TextPlacementKind::Absolute, {o.pos.x, o.pos.y}},
                .anchor = TextAnchor::Center,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .wrap = TextWrap::Word,
                .overflow = TextOverflow::Clip,
                .centering_mode = TextCenteringMode::PixelInk,
                .line_height = o.line_height,
                .tracking = o.tracking,
                .auto_fit = o.auto_fit,
                .min_font_size = o.min_font_size,
                .max_font_size = o.max_font_size,
                .max_lines = o.max_lines
            }
        };
    };

    if (raw_frame < 0.0f || total_chars_f <= 0.0f) {
        Color c = o.color;
        c.a = 0.0f;
        return make_base(std::string(" "), c);
    }

    const f32 linear_t = std::clamp(raw_frame * chars_per_frame / total_chars_f,
                                    0.0f, 1.0f);
    const f32 eased_t  = tw.easing.apply(linear_t);

    if (eased_t >= 1.0f) {
        return make_base(std::move(o.text), o.color);
    }

    const size_t revealed = static_cast<size_t>(eased_t * total_chars_f);
    const f32    partial  = (eased_t * total_chars_f) - static_cast<f32>(revealed);

    std::string visible = (revealed == 0)
        ? std::string(" ")
        : o.text.substr(0, grapheme_byte_offset_at(o.text, revealed));

    Color c = o.color;
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
    FontSpec font_spec;
    font_spec.font_path = opts.font_asset;
    font_spec.font_family = opts.font_family;
    font_spec.font_weight = opts.font_weight;

    TypewriterLayoutKey key;
    key.text = opts.text;
    key.font = font_identity_of(font_spec);
    key.font_size = opts.font_size;
    key.tracking = opts.tracking;
    key.box = opts.box;
    key.line_height = opts.line_height;

    auto& cache = engine.typewriter_layout_cache();
    std::shared_ptr<const TypewriterLayoutEntry> entry = cache.get(key);

    if (!entry) {
        PlacedGlyphRun placed;
        auto layout_result = compute_typewriter_layout(
            opts.text, opts.font_size, opts.tracking,
            opts.box, opts.line_height, font_spec,
            engine,
            &placed);
        // F0.3 — propagate structured error from compute_typewriter_layout
        if (!layout_result) return layout_result.error();

        TypewriterLayoutEntry new_entry;
        new_entry.layout = std::move(*layout_result);
        new_entry.placed = std::move(placed);
        new_entry.style = TypewriterStyle{
            opts.font_asset, opts.font_family, opts.font_weight,
            opts.font_size, opts.color, opts.line_height};
        new_entry.glyphs = detail::compile_typewriter_glyphs(
            new_entry.layout, new_entry.placed, opts.text, layer_prefix);

        entry = std::make_shared<TypewriterLayoutEntry>(std::move(new_entry));
        cache.put(key, entry);
    }

    if (entry->layout.chars.empty()) return TextError{
        TextErrorCode::NoLayoutChars,
        "typewriter_build: layout has zero characters"};

    const f32 total_chars = static_cast<f32>(entry->layout.chars.size());
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(opts.start_delay);

    // F0.3 — start-delay guard: not an error, normal operational state.
    if (raw_frame < 0.0f) return true;

    const f32 linear_t = std::clamp(raw_frame * opts.chars_per_frame / total_chars, 0.0f, 1.0f);
    const f32 eased_t = opts.easing.apply(linear_t);
    const f32 revealed_exact = eased_t * total_chars;
    const size_t revealed_count = static_cast<size_t>(revealed_exact);
    const f32 revealed_frac = revealed_exact - static_cast<f32>(revealed_count);

    for (size_t i = 0; i < entry->glyphs.size(); ++i) {
        const auto& glyph = entry->glyphs[i];

        f32 opacity;
        if (glyph.original_index < revealed_count) {
            opacity = 1.0f;
        } else if (glyph.original_index == revealed_count) {
            opacity = opts.fade_easing.apply(revealed_frac);
        } else {
            break;
        }

        if (opacity < 0.005f) continue;

        s.layer(glyph.layer_name, [entry, i, opacity](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(opacity);

            const auto& g = entry->glyphs[i];
            const auto& style = entry->style;

            // F2.D — canonical TextDefinition
            l.text("glyph", TextDefinition{
                .content = {.value = g.text_slice, .pre_shaped = g.run},
                .style = {.font = {.font_path = style.font_path,
                                   .font_family = style.font_family,
                                   .font_weight = style.font_weight,
                                   .font_size = style.font_size},
                          .color = style.color},
                .frame = {.size = {style.font_size * 2.0f, style.font_size * 2.0f},
                          .placement = TextPlacement{TextPlacementKind::Absolute, {g.placement.x, g.placement.y}},
                          .anchor = TextAnchor::Center,
                          .align = TextAlign::Center,
                          .vertical_align = VerticalAlign::Middle,
                          .wrap = TextWrap::None,
                          .overflow = TextOverflow::Clip,
                          .centering_mode = TextCenteringMode::PixelInk,
                          .line_height = style.line_height,
                          .tracking = 0.0f},
            });
        });
    }
    return true;
}

} // namespace chronon3d::content::text
