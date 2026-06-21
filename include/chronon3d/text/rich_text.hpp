#pragma once

// ---------------------------------------------------------------------------
// rich_text.hpp
//
// Rich text types and rendering: RichTextLine, draw_rich_text(), and
// associated enums, metrics, and layout options.
//
// Extracted from design_kit.hpp for single-responsibility.
// ---------------------------------------------------------------------------

#include <chronon3d/math/color.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

// ── Enums ───────────────────────────────────────────────────────────────────

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

// ── Data types ──────────────────────────────────────────────────────────────

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
    Vec4 ink_bounds{0.0f, 0.0f, 0.0f, 0.0f};
};

struct RichTextLineMetrics {
    f32 width{0.0f};
    f32 ascent{0.0f};
    f32 descent{0.0f};
    f32 height{0.0f};
    f32 baseline{0.0f};
    bool has_ink_bounds{false};
    Vec4 ink_bounds{0.0f, 0.0f, 0.0f, 0.0f};
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

// ── RichTextLine ────────────────────────────────────────────────────────────

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

    [[nodiscard]] RichTextLineMetrics measure(const FontEngine* engine = nullptr) const;

    [[nodiscard]] static RichTextRunMetrics measure_run(const RichTextRun& run, const FontEngine* engine);

private:
    std::vector<RichTextRun> m_runs;
};

// ── draw_rich_text (kept inline — depends on LayerBuilder from chronon3d_scene) ──

inline RichTextLineMetrics draw_rich_text(LayerBuilder& l,
                                          const RichTextLine& line,
                                          Vec3 origin,
                                          const RichTextLayoutOptions& options = {},
                                          FontEngine* engine = nullptr,
                                          std::string_view prefix = "rich") {
    // WP-8 PR 8.0: callers MUST supply a FontEngine* bound to an AssetResolver.
    // `shared_font_engine()` was deleted.  We do NOT `assert()` here because
    // asserts compile out under NDEBUG and would crash the production binary;
    // we early-return empty metrics so a missing engine produces a no-op draw
    // (matches the pre-PR-8.0 fall-back behaviour for adapters that haven't
    // migrated yet — PR 8.1 will tighten this to throw or be removed).
    if (engine == nullptr) {
        return RichTextLineMetrics{};
    }
    FontEngine& font_engine = *engine;

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

            TextSpec p;
            p.content.value = run.text;
            p.layout.box = {
                std::max(0.0f, run.width + options.glyph_padding),
                std::max(0.0f, (layout_line.ascent + layout_line.descent) + options.glyph_padding)
            };
            p.position = {
                run_x - options.glyph_padding * 0.5f,
                baseline_y - run_metrics.ascent - options.glyph_padding * 0.5f,
                base_origin.z
            };
            if (options.snap_to_pixels) {
                p.position.x = std::round(p.position.x);
                p.position.y = std::round(p.position.y);
            }
            p.font.font_path = run.style.font_path;
            p.font.font_family = run.style.font_family;
            p.font.font_weight = run.style.font_weight;
            p.font.font_style = run.style.font_style;
            p.font.font_size = run.style.size;
            p.appearance.color = run.style.color;
            p.layout.align = TextAlign::Left;
            p.layout.vertical_align = VerticalAlign::Top;
            p.layout.tracking = run.style.tracking;
            p.layout.wrap = TextWrap::None;
            p.appearance.box_style.enabled = false;
            p.appearance.paint = run.style.paint;
            if (p.appearance.paint.fill == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(run.style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
                p.appearance.paint.fill = run.style.color;
            }
            p.appearance.material = run.style.material;
            l.text(std::move(node_name), p);
        }
    }

    return metrics;
}

} // namespace chronon3d
