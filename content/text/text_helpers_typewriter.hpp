#pragma once

// ── Typewriter Text Helpers ────────────────────────────────────────────────
// Extracted from text_helpers.hpp.
// typewriter_text(), typewriter_build(), compute_typewriter_layout()

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>

// Needed for complete CenterTextOptions type used by-value in typewriter_text()
#include "text_helpers_centered.hpp"
#include <chronon3d/text/text_definition.hpp>  // F2.C — canonical DTO

#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace chronon3d::content::text {

struct CenterTextOptions;

// ── UTF-8 code-point-safe helpers ──────────────────────────────────────

inline size_t utf8_code_point_count(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

inline size_t utf8_byte_offset_at(const std::string& s, size_t code_points) {
    size_t offset = 0;
    size_t count  = 0;

    while (offset < s.size() && count < code_points) {
        const unsigned char lead = static_cast<unsigned char>(s[offset]);

        size_t len = 1;
        if      ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;

        if (offset + len > s.size()) len = 1;

        offset += len;
        ++count;
    }

    return offset;
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. typewriter_text — simple substr-based reveal
// ═════════════════════════════════════════════════════════════════════════════

struct TypewriterOptions {
    EasingCurve easing{Easing::Linear};
    Frame       start_delay{0};
    f32         fade_chars{1.0f};
};

/// F2.C — canonical authoring helper.  Returns TextDefinition.
inline TextDefinition typewriter_text(CenterTextOptions o,
                                  Frame frame,
                                  f32 chars_per_frame = 1.5f,
                                  TypewriterOptions tw = {}) {
    using chronon3d::detail::grapheme_cluster_count;
    using chronon3d::detail::grapheme_byte_offset_at;

    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(tw.start_delay);
    const f32 total_chars_f = static_cast<f32>(grapheme_cluster_count(o.text));

    auto make_base = [&](std::string value, Color c) -> TextDefinition {
        return from_text_spec(TextSpec{
            .content    = {.value = std::move(value)},
            .font       = {.font_path   = std::move(o.font_asset),
                           .font_family = std::move(o.font_family),
                           .font_weight = o.font_weight,
                           .font_style  = std::move(o.font_style),
                           .font_size   = o.font_size},
            .layout     = {.box            = o.box,
                           .anchor         = TextAnchor::Center,
                           .centering_mode = TextCenteringMode::PixelInk,
                           .align          = TextAlign::Center,
                           .vertical_align = VerticalAlign::Middle,
                           .wrap           = TextWrap::Word,
                           .overflow       = TextOverflow::Clip,
                           .line_height    = o.line_height,
                           .tracking       = o.tracking,
                           .auto_fit       = o.auto_fit,
                           .min_font_size  = o.min_font_size,
                           .max_font_size  = o.max_font_size,
                           .max_lines      = o.max_lines},
            .appearance = {.color = c},
            .position   = o.pos,
        });
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

// ═════════════════════════════════════════════════════════════════════════════
// 3. typewriter_build — stable per-character typewriter
// ═════════════════════════════════════════════════════════════════════════════

struct TypewriterBuildOptions {
    std::string text;
    Vec2  box{1200.0f, 240.0f};
    f32   font_size{96.0f};
    f32   tracking{0.0f};
    f32   line_height{1.10f};
    std::string font_asset{"assets/fonts/Poppins-Bold.ttf"};  // asset-relative path
    std::string font_family{"Poppins"};
    int   font_weight{700};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32   chars_per_frame{1.0f};
    Frame start_delay{0};
    EasingCurve easing{Easing::Linear};
    EasingCurve fade_easing{Easing::OutCubic};
    Frame fade_duration{10};
};

struct TypewriterCharPos {
    size_t byte_offset;
    size_t byte_len;
    f32 x;
    f32 y;
    f32 advance;
};

struct TypewriterLayout {
    std::vector<TypewriterCharPos> chars;
    f32 total_width{0.0f};
    f32 total_height{0.0f};
};

// F0.2b — resolver-based overloads replaced by FontEngine&.
// Forward-declare: full implementation lives in this header.
TypewriterLayout compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec,
    FontEngine& engine,
    PlacedGlyphRun* out_placed = nullptr);

void typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame,
    FontEngine& engine);

} // namespace chronon3d::content::text

// ── Implementation of compute_typewriter_layout ────────────────────────────
// (must be after all includes and namespace declaration)

#include <chronon3d/text/glyph_atlas.hpp>  // for shared_font_engine etc.
#include <cmath>
#include <cstddef>

namespace chronon3d::content::text {

inline TypewriterLayout compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec,
    FontEngine& engine,
    PlacedGlyphRun* out_placed)
{
    TypewriterLayout result;
    if (text.empty()) return result;

    // F0.2b — FontEngine is now caller-supplied (wired from ctx.font_engine).

    auto run = engine.shape_text(text, font_spec, font_size);
    if (!run || run->glyphs.empty()) return result;

    auto placed = resolve_placed_glyph_run(*run, tracking, text);
    if (placed.clusters.empty()) return result;

    struct CharAdv { size_t byte_offset; size_t byte_len; f32 advance; };
    std::vector<CharAdv> char_advances;
    char_advances.reserve(placed.clusters.size());
    for (const auto& cl : placed.clusters) {
        CharAdv ca;
        ca.byte_offset = cl.byte_offset;
        ca.byte_len = cl.byte_len;
        ca.advance = cl.raw_advance;
        char_advances.push_back(ca);
    }

    // Grapheme-cluster merge
    {
        std::vector<CharAdv> merged;
        merged.reserve(char_advances.size());

        enum class GB11State { Normal, ExtPict, ExtPictZWJ };
        GB11State gb11 = GB11State::Normal;

        for (size_t i = 0; i < char_advances.size(); ++i) {
            auto& ca = char_advances[i];

            size_t cp_offset = ca.byte_offset;
            if (cp_offset < text.size()) {
                const char32_t cp = chronon3d::detail::utf8_decode_cp(std::string_view(text), cp_offset);
                const bool is_ext = chronon3d::detail::is_grapheme_extend(cp);
                const bool is_ep  = chronon3d::detail::is_extended_pictographic(cp);
                const bool is_ri  = chronon3d::detail::is_regional_indicator(cp);
                const bool is_zwj = (cp == 0x200D);

                if (!merged.empty() && is_ri) {
                    size_t prev_off = merged.back().byte_offset;
                    if (prev_off < text.size()) {
                        const char32_t prev_cp = chronon3d::detail::utf8_decode_cp(
                            std::string_view(text), prev_off);
                        if (chronon3d::detail::is_regional_indicator(prev_cp)) {
                            auto& prev = merged.back();
                            prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                            prev.advance += ca.advance;
                            gb11 = GB11State::Normal;
                            continue;
                        }
                    }
                }

                if (is_zwj && gb11 == GB11State::ExtPict) {
                    gb11 = GB11State::ExtPictZWJ;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                if (is_ep && !is_ri && gb11 == GB11State::ExtPictZWJ) {
                    gb11 = GB11State::ExtPict;
                    if (!merged.empty()) {
                        auto& prev = merged.back();
                        prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                        prev.advance += ca.advance;
                    }
                    continue;
                }

                if (is_ext && !merged.empty()) {
                    auto& prev = merged.back();
                    prev.byte_len = ca.byte_offset + ca.byte_len - prev.byte_offset;
                    prev.advance += ca.advance;
                    continue;
                }

                if (!is_ext) {
                    gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
                }
            }

            merged.push_back(ca);
        }

        char_advances = std::move(merged);
    }

    // Word-wrap
    struct Line { size_t begin_idx; size_t end_idx; f32 width; };
    std::vector<Line> lines;

    size_t line_start = 0;
    f32    line_width = 0.0f;
    size_t word_boundary = 0;
    f32    width_at_boundary = 0.0f;

    for (size_t i = 0; i < char_advances.size(); ++i) {
        auto& ca = char_advances[i];
        bool is_space = (ca.byte_len == 1 && text[ca.byte_offset] == ' ');
        bool is_newline = (ca.byte_len == 1 && text[ca.byte_offset] == '\n');

        if (is_newline) {
            lines.push_back({line_start, i, line_width});
            line_start = i + 1;
            line_width = 0.0f;
            word_boundary = i + 1;
            width_at_boundary = 0.0f;
            continue;
        }
        if (is_space) {
            word_boundary = i + 1;
            width_at_boundary = line_width + ca.advance;
        }
        if (line_width + ca.advance > box.x && i > line_start) {
            if (word_boundary > line_start) {
                f32 lw = width_at_boundary;
                if (word_boundary > 0) lw -= char_advances[word_boundary - 1].advance;
                lines.push_back({line_start, word_boundary - 1, lw});
                line_start = word_boundary;
                line_width = 0.0f;
                for (size_t j = word_boundary; j < i; ++j)
                    line_width += char_advances[j].advance;
                word_boundary = line_start;
                width_at_boundary = 0.0f;
            } else {
                lines.push_back({line_start, i, line_width});
                line_start = i;
                line_width = 0.0f;
                word_boundary = i;
                width_at_boundary = 0.0f;
            }
        }
        line_width += ca.advance;
    }
    if (line_start < char_advances.size())
        lines.push_back({line_start, char_advances.size(), line_width});

    // Apply tracking per line
    for (auto& ln : lines) {
        if (tracking != 0.0f && ln.end_idx > ln.begin_idx) {
            f32 line_tracking = 0.0f;
            for (size_t i = ln.begin_idx; i + 1 < ln.end_idx; ++i) {
                char_advances[i].advance += tracking;
                line_tracking += tracking;
            }
            ln.width += line_tracking;
        }
    }

    const f32 line_step = std::max(1.0f, font_size * line_height);

    if (box.y > 0.0f) {
        size_t max_lines = std::max(static_cast<size_t>(box.y / line_step), size_t{1});
        if (lines.size() > max_lines) lines.resize(max_lines);
    }
    if (lines.empty()) return result;

    f32 max_w = 0.0f;
    for (auto& ln : lines) max_w = std::max(max_w, ln.width);
    f32 total_h = static_cast<f32>(lines.size()) * line_step;
    result.total_width = max_w;
    result.total_height = total_h;

    f32 y_base = -total_h * 0.5f + line_step * 0.5f;
    for (size_t li = 0; li < lines.size(); ++li) {
        auto& ln = lines[li];
        f32 x = -ln.width * 0.5f;
        f32 y = y_base + static_cast<f32>(li) * line_step;
        for (size_t i = ln.begin_idx; i < ln.end_idx; ++i) {
            auto& ca = char_advances[i];
            TypewriterCharPos cp;
            cp.byte_offset = ca.byte_offset;
            cp.byte_len = ca.byte_len;
            cp.advance = ca.advance;
            cp.x = x + ca.advance * 0.5f;
            cp.y = y;
            result.chars.push_back(cp);
            x += ca.advance;
        }
    }

    if (out_placed) {
        *out_placed = resolve_placed_glyph_run(*run, 0.0f, text);
    }

    return result;
}

// ── typewriter_build — implementation ─────────────────────────────────────

inline void typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame,
    FontEngine& engine)
{
    FontSpec font_spec;
    font_spec.font_path = opts.font_asset;
    font_spec.font_family = opts.font_family;
    font_spec.font_weight = opts.font_weight;

    static std::mutex s_cache_mutex;
    static TypewriterLayout cached_layout;
    static PlacedGlyphRun cached_placed;
    static std::string cached_text;
    static f32         cached_font_size{0.0f};
    static f32         cached_tracking{0.0f};
    static Vec2        cached_box{0.0f, 0.0f};
    static f32         cached_line_height{0.0f};
    static FontSpec    cached_font_spec;
    // F0.2b — cached_resolver (pointer-identity) cache key tier replaced
    // by FontEngine* identity; same lifetime guarantee.
    static const FontEngine* cached_engine{nullptr};

    std::lock_guard<std::mutex> lock(s_cache_mutex);

    bool cache_hit = (cached_engine == &engine &&
                      cached_text == opts.text &&
                      cached_font_size == opts.font_size &&
                      cached_tracking == opts.tracking &&
                      cached_box.x == opts.box.x &&
                      cached_box.y == opts.box.y &&
                      cached_line_height == opts.line_height &&
                      cached_font_spec.font_path == font_spec.font_path &&
                      cached_font_spec.font_family == font_spec.font_family &&
                      cached_font_spec.font_weight == font_spec.font_weight);

    if (!cache_hit) {
        cached_placed = PlacedGlyphRun{};
        cached_layout = compute_typewriter_layout(
            opts.text, opts.font_size, opts.tracking,
            opts.box, opts.line_height, font_spec,
            engine,
            &cached_placed);
        cached_text = opts.text;
        cached_font_size = opts.font_size;
        cached_tracking = opts.tracking;
        cached_box = opts.box;
        cached_line_height = opts.line_height;
        cached_font_spec = font_spec;

        // F0.2b — FontEngine pointer-identity replaces AssetResolver
        // pointer-identity as the cache key tier.
        cached_engine = &engine;
    }

    auto& layout = cached_layout;

    if (layout.chars.empty()) return;

    const f32 total_chars = static_cast<f32>(layout.chars.size());
    const f32 raw_frame = static_cast<f32>(frame) - static_cast<f32>(opts.start_delay);

    if (raw_frame < 0.0f) return;

    const f32 linear_t = std::clamp(raw_frame * opts.chars_per_frame / total_chars, 0.0f, 1.0f);
    const f32 eased_t = opts.easing.apply(linear_t);
    const f32 revealed_exact = eased_t * total_chars;
    const size_t revealed_count = static_cast<size_t>(revealed_exact);
    const f32 revealed_frac = revealed_exact - static_cast<f32>(revealed_count);

    for (size_t i = 0; i < layout.chars.size(); ++i) {
        auto& cp = layout.chars[i];

        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == ' ') continue;
        if (cp.byte_len == 1 && opts.text[cp.byte_offset] == '\n') continue;

        f32 opacity;
        if (i < revealed_count) {
            opacity = 1.0f;
        } else if (i == revealed_count) {
            opacity = opts.fade_easing.apply(revealed_frac);
        } else {
            break;
        }

        if (opacity < 0.005f) continue;

        std::shared_ptr<PlacedGlyphRun> char_placed;
        if (!cached_placed.clusters.empty() && !cached_placed.glyphs.empty()) {
            const size_t char_start = cp.byte_offset;
            const size_t char_end = cp.byte_offset + cp.byte_len;

            size_t first_cl = cached_placed.clusters.size();
            size_t last_cl = 0;
            for (size_t ci = 0; ci < cached_placed.clusters.size(); ++ci) {
                const auto& cl = cached_placed.clusters[ci];
                const size_t cl_start = cl.byte_offset;
                const size_t cl_end = cl.byte_offset + cl.byte_len;
                if (cl_start < char_end && cl_end > char_start) {
                    if (ci < first_cl) first_cl = ci;
                    if (ci > last_cl) last_cl = ci;
                }
            }

            if (last_cl >= first_cl && first_cl < cached_placed.clusters.size()) {
                const auto& first_cluster = cached_placed.clusters[first_cl];
                const auto& last_cluster = cached_placed.clusters[last_cl];
                const size_t start_glyph = first_cluster.start_glyph;
                const size_t end_glyph = last_cluster.end_glyph;

                if (end_glyph > start_glyph && start_glyph < cached_placed.glyphs.size()) {
                    auto mini_run = std::make_shared<PlacedGlyphRun>();
                    mini_run->ascent = cached_placed.ascent;
                    mini_run->descent = cached_placed.descent;
                    mini_run->baseline = cached_placed.baseline;
                    mini_run->font_size = cached_placed.font_size;

                    const float base_x = cached_placed.glyphs[start_glyph].x;
                    const float base_y = cached_placed.glyphs[start_glyph].y;
                    for (size_t gi = start_glyph; gi < end_glyph; ++gi) {
                        const auto& src = cached_placed.glyphs[gi];
                        PlacedGlyph pg = src;
                        pg.x = src.x - base_x;
                        pg.y = src.y - base_y;
                        mini_run->glyphs.push_back(pg);
                        mini_run->total_width += src.advance_x;
                    }

                    PlacedGlyphRun::Cluster mini_cl;
                    mini_cl.start_glyph = 0;
                    mini_cl.end_glyph = mini_run->glyphs.size();
                    mini_cl.byte_offset = cp.byte_offset;
                    mini_cl.byte_len = cp.byte_len;
                    for (const auto& pg : mini_run->glyphs) {
                        mini_cl.advance += pg.advance_x;
                        mini_cl.raw_advance += pg.raw_advance_x;
                    }
                    mini_run->clusters.push_back(mini_cl);
                    mini_run->total_height = cached_placed.total_height;

                    char_placed = std::move(mini_run);
                }
            }
        }

        std::string glyph = opts.text.substr(cp.byte_offset, cp.byte_len);
        std::string lname = std::string(layer_prefix) + "_c" + std::to_string(i);

        s.layer(lname, [cp, glyph, opacity, char_placed,
                        fp = opts.font_asset, ff = opts.font_family,
                        fw = opts.font_weight, fs = opts.font_size,
                        col = opts.color, lh = opts.line_height](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(opacity);

            // F2.D — canonical TextDefinition instead of TextSpec
            l.text("glyph", TextDefinition{
                .content = {.value = glyph, .pre_shaped = char_placed},
                .style = {.font = {.font_path = fp, .font_family = ff,
                                   .font_weight = fw, .font_size = fs},
                          .color = col},
                .frame = {.size = {fs * 2.0f, fs * 2.0f},
                          .anchor = TextAnchor::Center,
                          .centering_mode = TextCenteringMode::PixelInk,
                          .align = TextAlign::Center,
                          .vertical_align = VerticalAlign::Middle,
                          .wrap = TextWrap::None,
                          .overflow = TextOverflow::Clip,
                          .line_height = lh,
                          .tracking = 0.0f,
                          .position = {cp.x, cp.y, 0.0f}},
            });
        });
    }
}

// ── compute_single_line_glyph_layout ──────────────────────────────────────

inline TypewriterLayout compute_single_line_glyph_layout(
    const std::string& text,
    f32 font_size,
    f32 tracking,
    const FontSpec& font,
    FontEngine& engine)
{
    return compute_typewriter_layout(
        text,
        font_size,
        tracking,
        Vec2{100000.0f, font_size * 2.0f},
        1.0f,
        font,
        engine
    );
}

} // namespace chronon3d::content::text
