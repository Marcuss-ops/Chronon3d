#include <chronon3d/text/rich_text.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

RichTextLineMetrics RichTextLine::measure(const FontEngine* engine) const {
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

RichTextRunMetrics RichTextLine::measure_run(const RichTextRun& run, const FontEngine* engine) {
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
            const size_t char_count = run.text().size();
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

} // namespace chronon3d
