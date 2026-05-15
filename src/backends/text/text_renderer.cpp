#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

bool TextRenderer::draw_text(const TextShape& t, const Transform& tr, Framebuffer& fb,
                             const RenderState* state) {
    if (t.text.empty() || t.style.font_path.empty()) return true;

    const CachedFont* font_entry = m_cache.get_or_load(t.style.font_path);
    if (!font_entry) return false;

    auto backend = m_cache.get_backend();
    if (!backend) {
        spdlog::error("TextRenderer: no backend set");
        return false;
    }

    // Rotation is not yet supported; log once if encountered.
    if (!(tr.rotation.w > 0.9999f)) {
        static bool logged = false;
        if (!logged) {
            spdlog::warn("Text rotation not yet supported (deferred to Transform 2)");
            logged = true;
        }
    }

    const float effective_size = t.style.size * tr.scale.x;

    // Helper: render one line of text at a given (x, baseline_y).
    auto draw_line_at = [&](const std::string& line_text, float start_x, float baseline_y,
                             float line_size) {
        float cur_x = start_x;
        for (size_t i = 0; i < line_text.size(); ++i) {
            const char c = line_text[i];
            text::GlyphMetrics metrics;
            auto bitmap = backend->render_glyph(t.style.font_path, c, line_size, metrics);
            
            if (bitmap && bitmap->pixels) {
                const int dx = (int)std::floor(cur_x + metrics.offset.x);
                const int dy = (int)std::floor(baseline_y + metrics.offset.y);
                const int gw = bitmap->width;
                const int gh = bitmap->height;

                for (int by = 0; by < gh; ++by) {
                    const int py = dy + by;
                    if (py < 0 || py >= fb.height()) continue;
                    for (int bx = 0; bx < gw; ++bx) {
                        const int px = dx + bx;
                        if (px < 0 || px >= fb.width()) continue;
                        const float ga = (float)bitmap->pixels[(size_t)(by * gw + bx)] / 255.0f;
                        if (ga <= 0.0f) continue;
                        if (state && state->mask && state->mask->enabled()) {
                            Vec4 loc = state->layer_inv_matrix *
                                       Vec4((f32)px, (f32)py, 0.0f, 1.0f);
                            if (!mask_contains_local_point(*state->mask, Vec2{loc.x, loc.y})) continue;
                        }
                        Color src = t.style.color;
                        src.a *= ga * tr.opacity;
                        fb.set_pixel(px, py, raster::blend_normal(src, fb.get_pixel(px, py)));
                    }
                }
            }
            cur_x += metrics.advance + t.style.tracking;
            // Kerning would go here if backend supported it, but we'll stick to advance for now.
        }
    };

    // Multi-line path: use TextLayoutEngine when TextBox is enabled.
    if (t.box.enabled) {
        auto cw = [&](char c, float sz) -> float {
            return backend->get_char_advance(t.style.font_path, c, sz);
        };
        TextLayoutInput li;
        li.text       = t.text;
        li.style      = t.style;
        li.box        = t.box;
        li.char_width = cw;
        const auto result = TextLayoutEngine::layout(li);

        const float line_px      = result.resolved_font_size * t.style.line_height * tr.scale.x;
        const float render_size  = result.resolved_font_size * tr.scale.x;

        // Baseline calculation would need v-metrics from backend, but for now we'll approximate.
        // TODO: Add get_font_vmetrics to FontBackend.
        float y = tr.position.y + render_size * 0.8f; // approximation of ascent
        for (const auto& line : result.lines) {
            float x_off = 0.0f;
            if (t.style.align == TextAlign::Center) x_off = -line.width * 0.5f;
            else if (t.style.align == TextAlign::Right) x_off = -line.width;
            draw_line_at(line.text, tr.position.x + x_off, y, render_size);
            y += line_px;
        }
        return true;
    }

    // Single-line fallback.
    float text_width = 0.0f;
    for (size_t i = 0; i < t.text.size(); ++i) {
        text_width += backend->get_char_advance(t.style.font_path, t.text[i], effective_size) + t.style.tracking;
    }

    float x_offset = 0.0f;
    if (t.style.align == TextAlign::Center) x_offset = -text_width * 0.5f;
    else if (t.style.align == TextAlign::Right)  x_offset = -text_width;

    const float start_x    = tr.position.x + x_offset;
    const float baseline_y = tr.position.y + effective_size * 0.8f;
    draw_line_at(t.text, start_x, baseline_y, effective_size);
    return true;
}

} // namespace chronon3d
