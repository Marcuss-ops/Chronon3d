#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

enum class RichTextAnchor {
    Left,
    Center,
    Right,
};

enum class RichTextVerticalAnchor {
    Top,
    Middle,
    Baseline,
    Bottom,
};

enum class RichTextRunKind {
    Text,
    Space,
    Star,
};

struct RichTextRun {
    RichTextRunKind kind{RichTextRunKind::Text};
    std::string text;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    FontSpec font{};
    f32 font_size{72.0f};
    f32 tracking{0.0f};
    f32 advance{0.0f};
    f32 star_inner_radius{12.0f};
    f32 star_outer_radius{42.0f};
    i32 star_points{8};
    TextPaint paint{};
    TextMaterial material{};
};

struct RichTextRunMetrics {
    f32 advance{0.0f};
    f32 ascent{0.0f};
    f32 descent{0.0f};
    f32 width{0.0f};
    f32 height{0.0f};
    bool has_ink_bounds{false};
    Vec4 ink_bounds{0.0f, 0.0f, 0.0f, 0.0f}; // left, top, right, bottom in baseline space
};

struct RichTextLineMetrics {
    f32 width{0.0f};
    f32 ascent{0.0f};
    f32 descent{0.0f};
    f32 height{0.0f};
    f32 baseline{0.0f};
    bool has_ink_bounds{false};
    Vec4 ink_bounds{0.0f, 0.0f, 0.0f, 0.0f}; // left, top, right, bottom in baseline space
};

struct RichTextLayoutOptions {
    Vec3 origin{0.0f, 0.0f, 0.0f};
    RichTextAnchor anchor{RichTextAnchor::Left};
    RichTextVerticalAnchor vertical_anchor{RichTextVerticalAnchor::Top};
    f32 glyph_padding{4.0f};
    bool snap_to_pixels{true};
    f32 max_width{0.0f};
    bool fit_to_width{false};
};

class RichTextLine {
public:
    RichTextLine& run(std::string text,
                      Color color,
                      f32 size = 72.0f,
                      std::string font = "assets/fonts/Inter-Bold.ttf") {
        RichTextRun r;
        r.kind = RichTextRunKind::Text;
        r.text = std::move(text);
        r.color = color;
        r.font.font_path = std::move(font);
        r.font.font_family = "Inter";
        r.font.font_weight = 800;
        r.font.font_style = "normal";
        r.font_size = size;
        r.paint.fill = color;
        m_runs.push_back(std::move(r));
        return *this;
    }

    RichTextLine& paint(TextPaint value) {
        if (!m_runs.empty()) {
            m_runs.back().paint = std::move(value);
        }
        return *this;
    }

    RichTextLine& material(TextMaterial value) {
        if (!m_runs.empty()) {
            m_runs.back().material = std::move(value);
        }
        return *this;
    }

    RichTextLine& size(f32 value) {
        if (!m_runs.empty()) {
            m_runs.back().font_size = value;
        }
        return *this;
    }

    RichTextLine& tracking(f32 value) {
        if (!m_runs.empty()) {
            m_runs.back().tracking = value;
        }
        return *this;
    }

    RichTextLine& space(f32 px) {
        RichTextRun r;
        r.kind = RichTextRunKind::Space;
        r.advance = std::max(0.0f, px);
        m_runs.push_back(std::move(r));
        return *this;
    }

    RichTextLine& star(Color color,
                       f32 outer_radius = 42.0f,
                       f32 inner_radius = 12.0f,
                       i32 points = 8) {
        RichTextRun r;
        r.kind = RichTextRunKind::Star;
        r.color = color;
        r.star_outer_radius = outer_radius;
        r.star_inner_radius = inner_radius;
        r.star_points = points;
        m_runs.push_back(std::move(r));
        return *this;
    }

    [[nodiscard]] const std::vector<RichTextRun>& runs() const { return m_runs; }

    [[nodiscard]] RichTextLineMetrics measure(const FontEngine* engine = nullptr) const {
        RichTextLineMetrics metrics;

        for (const auto& run : m_runs) {
            const RichTextRunMetrics run_metrics = measure_run(run, engine);
            metrics.width += run_metrics.advance;
            metrics.ascent = std::max(metrics.ascent, run_metrics.ascent);
            metrics.descent = std::max(metrics.descent, run_metrics.descent);
            if (run_metrics.has_ink_bounds) {
                if (!metrics.has_ink_bounds) {
                    metrics.ink_bounds = run_metrics.ink_bounds;
                    metrics.has_ink_bounds = true;
                } else {
                    metrics.ink_bounds.x = std::min(metrics.ink_bounds.x, run_metrics.ink_bounds.x);
                    metrics.ink_bounds.y = std::max(metrics.ink_bounds.y, run_metrics.ink_bounds.y);
                    metrics.ink_bounds.z = std::max(metrics.ink_bounds.z, run_metrics.ink_bounds.z);
                    metrics.ink_bounds.w = std::min(metrics.ink_bounds.w, run_metrics.ink_bounds.w);
                }
            }
        }

        metrics.height = metrics.ascent + metrics.descent;
        metrics.baseline = metrics.ascent;
        return metrics;
    }

public:
    [[nodiscard]] static RichTextRunMetrics measure_run(const RichTextRun& run, const FontEngine* engine) {
        RichTextRunMetrics metrics;

        switch (run.kind) {
            case RichTextRunKind::Space:
                metrics.advance = run.advance;
                break;

            case RichTextRunKind::Star:
                metrics.advance = std::max(0.0f, run.star_outer_radius * 2.0f);
                metrics.ascent = std::max(0.0f, run.star_outer_radius);
                metrics.descent = std::max(0.0f, run.star_outer_radius);
                metrics.has_ink_bounds = true;
                metrics.ink_bounds = Vec4{-run.star_outer_radius,
                                           run.star_outer_radius,
                                           run.star_outer_radius,
                                           -run.star_outer_radius};
                break;

            case RichTextRunKind::Text:
            default: {
                const float font_size = std::max(1.0f, run.font_size);
                const size_t char_count = run.text.size();
                if (engine) {
                    const auto font_metrics = engine->get_font_metrics(run.font, font_size);
                    metrics.advance = std::max(0.0f, engine->measure_text(run.text, run.font, font_size));
                    metrics.advance += run.tracking * static_cast<float>(char_count);
                    metrics.ascent = std::max(0.0f, font_metrics.ascent);
                    metrics.descent = std::max(0.0f, font_metrics.descent);
                    metrics.height = std::max(0.0f, font_metrics.line_height);
                    if (auto shaped = engine->shape_text(run.text, run.font, font_size)) {
                        f32 min_x = 0.0f;
                        f32 max_x = std::max(0.0f, shaped->width);
                        f32 top = 0.0f;
                        f32 bottom = 0.0f;
                        bool first = true;
                        for (const auto& glyph : shaped->glyphs) {
                            const f32 gx0 = glyph.x + glyph.bbox_x0;
                            const f32 gy_top = glyph.bbox_y0;
                            const f32 gx1 = glyph.x + glyph.bbox_x1;
                            const f32 gy_bottom = glyph.bbox_y1;
                            min_x = std::min(min_x, std::min(gx0, gx1));
                            max_x = std::max(max_x, std::max(gx0, gx1));
                            if (first) {
                                top = gy_top;
                                bottom = gy_bottom;
                                first = false;
                            } else {
                                top = std::max(top, gy_top);
                                bottom = std::min(bottom, gy_bottom);
                            }
                        }
                        metrics.has_ink_bounds = true;
                        metrics.ink_bounds = Vec4{min_x, top, max_x, bottom};
                    }
                } else {
                    metrics.advance = std::max(0.0f, static_cast<float>(char_count) * font_size * 0.6f);
                    metrics.advance += run.tracking * static_cast<float>(char_count);
                    metrics.ascent = font_size * 0.78f;
                    metrics.descent = font_size * 0.22f;
                    metrics.height = metrics.ascent + metrics.descent;
                    metrics.has_ink_bounds = true;
                    metrics.ink_bounds = Vec4{0.0f, metrics.ascent, metrics.advance, -metrics.descent};
                }
                break;
            }
        }

        if (metrics.height <= 0.0f) {
            metrics.height = metrics.ascent + metrics.descent;
        }
        return metrics;
    }

private:
    std::vector<RichTextRun> m_runs;
};

struct LayoutElement {
    Vec2 size{0.0f, 0.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] inline Vec2 hstack_size(const std::vector<Vec2>& sizes, f32 gap) {
    if (sizes.empty()) return {0.0f, 0.0f};
    f32 width = 0.0f;
    f32 height = 0.0f;
    for (size_t i = 0; i < sizes.size(); ++i) {
        width += sizes[i].x;
        height = std::max(height, sizes[i].y);
        if (i + 1 < sizes.size()) {
            width += gap;
        }
    }
    return {width, height};
}

[[nodiscard]] inline Vec2 vstack_size(const std::vector<Vec2>& sizes, f32 gap) {
    if (sizes.empty()) return {0.0f, 0.0f};
    f32 width = 0.0f;
    f32 height = 0.0f;
    for (size_t i = 0; i < sizes.size(); ++i) {
        width = std::max(width, sizes[i].x);
        height += sizes[i].y;
        if (i + 1 < sizes.size()) {
            height += gap;
        }
    }
    return {width, height};
}

[[nodiscard]] inline std::vector<LayoutElement> hstack(const std::vector<Vec2>& sizes, f32 gap, Vec3 start_pos = {0.0f, 0.0f, 0.0f}) {
    std::vector<LayoutElement> elements;
    elements.reserve(sizes.size());
    f32 current_x = start_pos.x;
    for (const auto& sz : sizes) {
        elements.push_back({
            .size = sz,
            .pos = {current_x + sz.x * 0.5f, start_pos.y + sz.y * 0.5f, start_pos.z}
        });
        current_x += sz.x + gap;
    }
    return elements;
}

[[nodiscard]] inline std::vector<LayoutElement> vstack(const std::vector<Vec2>& sizes, f32 gap, Vec3 start_pos = {0.0f, 0.0f, 0.0f}) {
    std::vector<LayoutElement> elements;
    elements.reserve(sizes.size());
    f32 current_y = start_pos.y;
    for (const auto& sz : sizes) {
        elements.push_back({
            .size = sz,
            .pos = {start_pos.x + sz.x * 0.5f, current_y + sz.y * 0.5f, start_pos.z}
        });
        current_y += sz.y + gap;
    }
    return elements;
}

struct BoxPadding {
    f32 left{0.0f};
    f32 top{0.0f};
    f32 right{0.0f};
    f32 bottom{0.0f};
};

[[nodiscard]] inline Vec2 box_fit_content(Vec2 content_size,
                                           BoxPadding padding = {},
                                           Vec2 min_size = {0.0f, 0.0f},
                                           std::optional<Vec2> max_size = std::nullopt) {
    Vec2 size{
        content_size.x + padding.left + padding.right,
        content_size.y + padding.top + padding.bottom
    };
    size.x = std::max(size.x, min_size.x);
    size.y = std::max(size.y, min_size.y);
    if (max_size) {
        size.x = std::min(size.x, max_size->x);
        size.y = std::min(size.y, max_size->y);
    }
    return size;
}

struct PremiumPillStyle {
    Vec2 size{900.0f, 120.0f};
    f32 radius{60.0f};
    Color fill{0.03f, 0.01f, 0.05f, 0.14f};
    Color stroke{0.78f, 0.48f, 0.88f, 0.28f};
    f32 stroke_width{1.0f};
    StrokeAlignment stroke_alignment{StrokeAlignment::Center};
};

inline void draw_stroked_rounded_rect(LayerBuilder& l,
                                      std::string name,
                                      Vec2 size,
                                      f32 radius,
                                      Color fill_color,
                                      Color stroke_color,
                                      f32 stroke_width,
                                      StrokeAlignment stroke_alignment = StrokeAlignment::Center) {
    RoundedRectParams p;
    p.size = size;
    p.radius = radius;
    p.color = fill_color;
    p.fill = Fill::solid_color(fill_color);
    p.stroke.enabled = stroke_width > 0.0f;
    p.stroke.color = stroke_color;
    p.stroke.width = stroke_width;
    p.stroke.alignment = stroke_alignment;
    l.rounded_rect(std::move(name), p);
}

inline void draw_stroked_circle(LayerBuilder& l,
                                std::string name,
                                f32 radius,
                                Color fill_color,
                                Color stroke_color,
                                f32 stroke_width) {
    CircleParams p;
    p.radius = radius;
    p.color = fill_color;
    p.fill = Fill::solid_color(fill_color);
    p.stroke.enabled = stroke_width > 0.0f;
    p.stroke.color = stroke_color;
    p.stroke.width = stroke_width;
    p.stroke.alignment = StrokeAlignment::Center;
    l.circle(std::move(name), p);
}

inline void draw_premium_pill(LayerBuilder& l, std::string name, const PremiumPillStyle& style) {
    draw_stroked_rounded_rect(
        l,
        std::move(name),
        style.size,
        style.radius,
        style.fill,
        style.stroke,
        style.stroke_width,
        style.stroke_alignment
    );
}

inline RichTextLineMetrics draw_rich_text(LayerBuilder& l,
                                          const RichTextLine& line,
                                          Vec3 origin,
                                          const RichTextLayoutOptions& options = {},
                                          FontEngine* engine = nullptr,
                                          std::string_view prefix = "rich") {
    FontEngine& font_engine = engine ? *engine : shared_font_engine();

    RichTextLineMetrics base_metrics = line.measure(&font_engine);
    f32 fit_scale = 1.0f;
    if (options.fit_to_width && options.max_width > 0.0f && base_metrics.width > options.max_width) {
        fit_scale = std::max(0.0f, options.max_width / std::max(1e-3f, base_metrics.width));
    }

    TextLayoutInput layout_input;
    layout_input.style.size = 72.0f;
    layout_input.style.align = TextAlign::Left;
    layout_input.style.vertical_align = VerticalAlign::Top;
    layout_input.style.wrap = TextWrap::None;
    layout_input.font_engine = &font_engine;

    for (const auto& run : line.runs()) {
        TextLayoutRun layout_run;
        layout_run.text = run.text;
        layout_run.is_space = (run.kind == RichTextRunKind::Space);
        layout_run.is_decorative_star = (run.kind == RichTextRunKind::Star);
        layout_run.star_inner_radius = run.star_inner_radius;
        layout_run.star_outer_radius = run.star_outer_radius;
        layout_run.star_points = run.star_points;
        layout_run.style.font_path = run.font.font_path;
        layout_run.style.font_family = run.font.font_family;
        layout_run.style.font_weight = run.font.font_weight;
        layout_run.style.font_style = run.font.font_style;
        layout_run.style.size = run.font_size * fit_scale;
        layout_run.style.tracking = run.tracking * fit_scale;
        layout_run.style.color = run.color;
        layout_run.style.paint = run.paint;
        layout_run.style.material = run.material;
        if (run.kind == RichTextRunKind::Space) {
            layout_run.use_advance_override = true;
            layout_run.advance_override = run.advance * fit_scale;
        } else if (run.kind == RichTextRunKind::Star) {
            layout_run.use_advance_override = true;
            layout_run.advance_override = std::max(0.0f, run.star_outer_radius * 2.0f * fit_scale);
            layout_run.star_inner_radius = run.star_inner_radius * fit_scale;
            layout_run.star_outer_radius = run.star_outer_radius * fit_scale;
        }
        layout_input.runs.push_back(std::move(layout_run));
    }

    const TextLayoutResult layout = TextLayoutEngine::layout(layout_input);
    RichTextLineMetrics metrics;
    if (!layout.lines.empty()) {
        metrics.width = layout.size.x;
        metrics.ascent = layout.lines.front().ascent;
        metrics.descent = layout.lines.front().descent;
        metrics.height = layout.size.y;
        metrics.baseline = layout.lines.front().baseline;
    } else {
        metrics = base_metrics;
    }
    if (!metrics.has_ink_bounds && base_metrics.has_ink_bounds) {
        metrics.has_ink_bounds = true;
        metrics.ink_bounds = base_metrics.ink_bounds;
    }
    if (metrics.baseline <= 0.0f) {
        metrics.baseline = metrics.ascent;
    }

    const Vec3 base_origin = origin + options.origin;

    f32 top_left_x = base_origin.x;
    f32 top_left_y = base_origin.y;

    if (options.anchor == RichTextAnchor::Center) {
        top_left_x -= metrics.width * 0.5f;
    } else if (options.anchor == RichTextAnchor::Right) {
        top_left_x -= metrics.width;
    }

    if (options.vertical_anchor == RichTextVerticalAnchor::Middle) {
        top_left_y -= metrics.height * 0.5f;
    } else if (options.vertical_anchor == RichTextVerticalAnchor::Baseline) {
        top_left_y -= metrics.baseline;
    } else if (options.vertical_anchor == RichTextVerticalAnchor::Bottom) {
        top_left_y -= metrics.height;
    }

    std::size_t index = 0;
    auto make_run_name = [&](std::size_t i) {
        return std::string(prefix) + "_" + std::to_string(i);
    };

    for (const auto& layout_line : layout.lines) {
        const f32 line_top_y = top_left_y + layout_line.position.y;
        const f32 baseline_y = line_top_y + layout_line.baseline;

        for (const auto& run : layout_line.runs) {
            std::string node_name = make_run_name(index++);
            const f32 run_x = top_left_x + layout_line.position.x + run.position.x;

            if (run.is_space) {
                continue;
            }

            if (run.is_decorative_star) {
                l.star(node_name, {
                    .center = {
                        run_x + run.width * 0.5f,
                        line_top_y + (layout_line.ascent + layout_line.descent) * 0.5f
                    },
                    .points = run.star_points,
                    .inner_radius = run.star_inner_radius,
                    .outer_radius = run.star_outer_radius,
                    .color = run.style.color
                });
                continue;
            }

            const RichTextRunMetrics run_metrics = RichTextLine::measure_run({
                .kind = RichTextRunKind::Text,
                .text = run.text,
                .color = run.style.color,
                .font = {
                    .font_path = run.style.font_path,
                    .font_family = run.style.font_family,
                    .font_weight = run.style.font_weight,
                    .font_style = run.style.font_style,
                },
                .font_size = run.style.size,
                .tracking = run.style.tracking,
                .paint = run.style.paint,
                .material = run.style.material,
            }, &font_engine);

            TextParams p;
            p.text = run.text;
            p.size = {
                std::max(0.0f, run.width + options.glyph_padding),
                std::max(0.0f, (layout_line.ascent + layout_line.descent) + options.glyph_padding)
            };
            p.pos = {
                run_x - options.glyph_padding * 0.5f,
                baseline_y - run_metrics.ascent - options.glyph_padding * 0.5f,
                base_origin.z
            };
            if (options.snap_to_pixels) {
                p.pos.x = std::round(p.pos.x);
                p.pos.y = std::round(p.pos.y);
            }
            p.font_path = run.style.font_path;
            p.font_family = run.style.font_family;
            p.font_weight = run.style.font_weight;
            p.font_style = run.style.font_style;
            p.font_size = run.style.size;
            p.color = run.style.color;
            p.align = TextAlign::Left;
            p.vertical_align = VerticalAlign::Top;
            p.tracking = run.style.tracking;
            p.wrap = TextWrap::None;
            p.box_style.enabled = false;
            p.paint = run.style.paint;
            if (p.paint.fill == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(run.style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
                p.paint.fill = run.style.color;
            }
            p.material = run.style.material;
            l.text(std::move(node_name), p);
        }
    }

    return metrics;
}

struct PostEffectPipeline {
    bool enable_bloom{false};
    f32 bloom_threshold{0.8f};
    f32 bloom_radius{24.0f};
    f32 bloom_intensity{0.6f};
    bool enable_tint{false};
    Color tint_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 tint_amount{0.0f};
};

inline void apply_post_pipeline(LayerBuilder& l, const PostEffectPipeline& pipe) {
    if (pipe.enable_bloom) {
        l.bloom(pipe.bloom_threshold, pipe.bloom_radius, pipe.bloom_intensity);
    }
    if (pipe.enable_tint) {
        l.tint(pipe.tint_color, pipe.tint_amount);
    }
}

inline void draw_premium_asterisk(LayerBuilder& l,
                                  std::string name,
                                  Vec3 pos,
                                  Color color,
                                  f32 outer_radius = 42.0f,
                                  f32 inner_radius = 12.0f) {
    l.star(std::move(name), {
        .center = {pos.x, pos.y},
        .points = 8,
        .inner_radius = inner_radius,
        .outer_radius = outer_radius,
        .color = color
    });
}

inline void draw_sparkle_4point(LayerBuilder& l,
                                std::string name,
                                Vec3 pos,
                                Color color,
                                f32 size = 30.0f) {
    l.star(std::move(name), {
        .center = {pos.x, pos.y},
        .points = 4,
        .inner_radius = size * 0.22f,
        .outer_radius = size,
        .color = color
    });
}

namespace Materials {

inline TextMaterial HotPinkFlatGlow() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {1.0f, 0.0f, 0.78f, 1.0f};
    m.bottom_color = {0.8f, 0.0f, 0.6f, 1.0f};
    m.emissive = 1.2f;
    return m;
}

inline TextMaterial GlossyBlueTitle() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {0.0f, 0.8f, 1.0f, 1.0f};
    m.bottom_color = {0.0f, 0.4f, 0.8f, 1.0f};
    m.bevel_px = 1.5f;
    m.emissive = 1.1f;
    return m;
}

inline TextMaterial DarkGlassTitle() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {0.98f, 0.95f, 1.0f, 0.92f};
    m.bottom_color = {0.80f, 0.82f, 0.95f, 0.78f};
    m.bevel_px = 1.0f;
    m.top_highlight_opacity = 0.20f;
    m.bottom_shade_opacity = 0.10f;
    m.use_material_glow = true;
    m.glow_radius = 10.0f;
    m.glow_intensity = 0.45f;
    m.glow_color = {0.72f, 0.56f, 1.0f, 0.60f};
    return m;
}

} // namespace Materials

} // namespace chronon3d
