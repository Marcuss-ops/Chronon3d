#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <chronon3d/renderer/text/text_renderer.hpp>
#include <chronon3d/renderer/text/text_layout_engine.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask_utils.hpp>
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

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_entry->data.data(), 0)) return false;

    // Rotation is not yet supported; log once if encountered.
    if (!(tr.rotation.w > 0.9999f)) {
        static bool logged = false;
        if (!logged) {
            spdlog::warn("Text rotation not yet supported (deferred to Transform 2)");
            logged = true;
        }
    }

    const float effective_size = t.style.size * tr.scale.x;
    const float scale = stbtt_ScaleForPixelHeight(&font, effective_size);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    // Helper: render one line of text at a given (x, baseline_y).
    auto draw_line_at = [&](const std::string& line_text, float start_x, float baseline_y,
                             float line_scale) {
        float cur_x = start_x;
        for (size_t i = 0; i < line_text.size(); ++i) {
            const char c = line_text[i];
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, (int)c, &advance, &lsb);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, (int)c, line_scale, line_scale, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> bm(static_cast<size_t>(gw * gh));
                stbtt_MakeCodepointBitmap(&font, bm.data(), gw, gh, gw, line_scale, line_scale, (int)c);
                const int dx = (int)std::floor(cur_x + (float)x0);
                const int dy = (int)std::floor(baseline_y + (float)y0);
                for (int by = 0; by < gh; ++by) {
                    const int py = dy + by;
                    if (py < 0 || py >= fb.height()) continue;
                    for (int bx = 0; bx < gw; ++bx) {
                        const int px = dx + bx;
                        if (px < 0 || px >= fb.width()) continue;
                        const float ga = (float)bm[(size_t)(by * gw + bx)] / 255.0f;
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
            cur_x += (float)advance * line_scale + t.style.tracking;
            if (i + 1 < line_text.size())
                cur_x += (float)stbtt_GetCodepointKernAdvance(&font, (int)c, (int)line_text[i+1]) * line_scale;
        }
    };

    // Multi-line path: use TextLayoutEngine when TextBox is enabled.
    if (t.box.enabled) {
        auto cw = [&](char c, float sz) -> float {
            float s = stbtt_ScaleForPixelHeight(&font, sz);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, (int)c, &adv, &lsb);
            return (float)adv * s;
        };
        TextLayoutInput li;
        li.text       = t.text;
        li.style      = t.style;
        li.box        = t.box;
        li.char_width = cw;
        const auto result = TextLayoutEngine::layout(li);

        const float line_px      = result.resolved_font_size * t.style.line_height * tr.scale.x;
        const float render_scale = stbtt_ScaleForPixelHeight(&font, result.resolved_font_size * tr.scale.x);

        float y = tr.position.y + (float)ascent * render_scale;
        for (const auto& line : result.lines) {
            float x_off = 0.0f;
            if (t.style.align == TextAlign::Center) x_off = -line.width * 0.5f;
            else if (t.style.align == TextAlign::Right) x_off = -line.width;
            draw_line_at(line.text, tr.position.x + x_off, y, render_scale);
            y += line_px;
        }
        return true;
    }

    // Single-line fallback (original path).
    // Measure full text width for alignment
    float text_width = 0.0f;
    for (size_t i = 0; i < t.text.size(); ++i) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(t.text[i]), &adv, &lsb);
        text_width += static_cast<float>(adv) * scale;
        if (i + 1 < t.text.size()) {
            text_width += static_cast<float>(
                stbtt_GetCodepointKernAdvance(&font, static_cast<int>(t.text[i]),
                                              static_cast<int>(t.text[i + 1]))) * scale;
        }
    }

    float x_offset = 0.0f;
    if (t.style.align == TextAlign::Center) x_offset = -text_width * 0.5f;
    else if (t.style.align == TextAlign::Right)  x_offset = -text_width;

    const float start_x    = tr.position.x + x_offset;
    const float baseline_y = tr.position.y + static_cast<float>(ascent) * scale;
    draw_line_at(t.text, start_x, baseline_y, scale);
    return true;
}

} // namespace chronon3d
