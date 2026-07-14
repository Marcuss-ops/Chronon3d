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
        // P0-3 fix(text/cache): do NOT cache TypewriterStyle alongside the
        // geometry.  Style is applied at emit time from current opts.text
        // (used to derive text_slice) and the typewriter_build() caller
        // layer_prefix (used to derive layer_name).
        new_entry.glyphs = detail::compile_typewriter_glyphs(
            new_entry.layout, new_entry.placed, opts.text);

        entry = std::make_shared<TypewriterLayoutEntry>(std::move(new_entry));
        cache.put(key, entry);
    }

    if (entry->layout.chars.empty()) return TextError{
        TextErrorCode::NoLayoutChars,
        "typewriter_build: layout has zero characters"};

    const f32 total_chars = static_cast<f32>(entry->layout.chars.size());
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(opts.start_delay);

    // F0.3 — start-delay guard: not an error, normal operational state.
    // The caller is expected to call typewriter_build() every frame;
    // bool-as-void pattern: any value means Ok (Result<void,…> not available).
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

        // P0-3 fix(text/cache): derive layer_name per-call so cache hits
        // do NOT reuse stale prefixes (cross-composition).
        const std::string computed_layer_name =
            std::string(layer_prefix) + "_c" + std::to_string(glyph.original_index);

        // P0-3 SAFETY fix(text/cache): hoist all per-call options into scope-
        // local values BEFORE s.layer() so the closure is fully self-contained
        // (no capture-by-ref of opts).  Init-capture with std::move transfers
        // ownership of the std::string heap buffers INTO the closure at
        // lambda construction time, so SceneBuilder can defer lambda
        // execution indefinitely without dangling against typewriter_build()'s
        // `opts`.  Mirrors text_reveal.cpp pattern (which captures `d` by-value
        // for the same reason).  Per-call, string count is reduced to 3
        // std::moves (text_slice + font_asset + font_family) instead of N copies.
        const auto& cp_outer = entry->layout.chars[glyph.original_index];
        std::string  text_slice  = opts.text.substr(cp_outer.byte_offset, cp_outer.byte_len);
        std::string  font_asset  = opts.font_asset;
        std::string  font_family = opts.font_family;
        int          font_weight = opts.font_weight;
        f32          font_size_v = opts.font_size;
        Color        color       = opts.color;
        f32          line_height = opts.line_height;

        s.layer(computed_layer_name,
            [entry, i, opacity,
             text_slice  = std::move(text_slice),
             font_asset  = std::move(font_asset),
             font_family = std::move(font_family),
             font_weight, font_size_v, color, line_height](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.opacity(opacity);

                const auto& g = entry->glyphs[i];
                // F2.D — canonical TextDefinition; style fields come from
                // per-call captured values (NOT cached entry.style).
                l.text("glyph", TextDefinition{
                    .content = {.value = text_slice, .pre_shaped = g.run},
                    .style = {.font = {.font_path   = font_asset,
                                       .font_family = font_family,
                                       .font_weight = font_weight,
                                       .font_size   = font_size_v},
                              .color = color},
                    .frame = {.size = {font_size_v * 2.0f, font_size_v * 2.0f},
                              .placement = TextPlacement{TextPlacementKind::Absolute, {g.placement.x, g.placement.y}},
                              .anchor = TextAnchor::Center,
                              .align = TextAlign::Center,
                              .vertical_align = VerticalAlign::Middle,
                              .wrap = TextWrap::None,
                              .overflow = TextOverflow::Clip,
                              .centering_mode = TextCenteringMode::PixelInk,
                              .line_height = line_height,
                              .tracking = 0.0f},
                });
            });
    }
    return true;
}

} // namespace chronon3d::content::text
