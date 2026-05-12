#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <chronon3d/renderer/text_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask_utils.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
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

    float cur_x      = tr.position.x + x_offset;
    float baseline_y = tr.position.y + static_cast<float>(ascent) * scale;

    for (size_t i = 0; i < t.text.size(); ++i) {
        const char c = t.text[i];

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(c), &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(c), scale, scale, &x0, &y0, &x1, &y1);

        const int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            std::vector<unsigned char> bitmap(static_cast<size_t>(w * h));
            stbtt_MakeCodepointBitmap(&font, bitmap.data(), w, h, w, scale, scale, static_cast<int>(c));

            const int draw_x = static_cast<int>(std::floor(cur_x + static_cast<float>(x0)));
            const int draw_y = static_cast<int>(std::floor(baseline_y + static_cast<float>(y0)));

            for (int by = 0; by < h; ++by) {
                const int py = draw_y + by;
                if (py < 0 || py >= fb.height()) continue;
                for (int bx = 0; bx < w; ++bx) {
                    const int px = draw_x + bx;
                    if (px < 0 || px >= fb.width()) continue;

                    const float glyph_alpha = static_cast<float>(bitmap[static_cast<size_t>(by * w + bx)]) / 255.0f;
                    if (glyph_alpha <= 0.0f) continue;

                    if (state && state->mask && state->mask->enabled()) {
                        Vec4 local = state->layer_inv_matrix *
                                     Vec4(static_cast<f32>(px), static_cast<f32>(py), 0.0f, 1.0f);
                        if (!mask_contains_local_point(*state->mask, Vec2{local.x, local.y})) continue;
                    }

                    Color src = t.style.color;
                    src.a *= glyph_alpha * tr.opacity;
                    fb.set_pixel(px, py, raster::blend_normal(src, fb.get_pixel(px, py)));
                }
            }
        }

        cur_x += static_cast<float>(advance) * scale;
        if (i + 1 < t.text.size()) {
            cur_x += static_cast<float>(
                stbtt_GetCodepointKernAdvance(&font, static_cast<int>(c),
                                              static_cast<int>(t.text[i + 1]))) * scale;
        }
    }
    return true;
}

} // namespace chronon3d
