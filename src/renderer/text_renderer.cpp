#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <chronon3d/renderer/text_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>

namespace chronon3d {

namespace {
    // Helper to blend a single pixel (already exists in compositor but let's keep it robust here)
    inline Color blend_normal(Color src, Color dst) {
        const f32 inv = 1.0f - src.a;
        return Color{
            src.r * src.a + dst.r * inv,
            src.g * src.a + dst.g * inv,
            src.b * src.a + dst.b * inv,
            src.a + dst.a * inv
        };
    }
}

bool TextRenderer::read_font_file(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0, std::ios::beg);

    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

bool TextRenderer::draw_text(const TextShape& t, Framebuffer& framebuffer) {
    if (t.text.empty() || t.style.font_path.empty()) {
        return true; // Nothing to draw
    }

    std::vector<unsigned char> font_data;
    if (!read_font_file(t.style.font_path, font_data)) {
        return false;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_data.data(), 0)) {
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, t.style.size);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    float cur_x = t.position.x;
    float baseline_y = t.position.y + static_cast<float>(ascent) * scale;

    for (size_t i = 0; i < t.text.length(); ++i) {
        char c = t.text[i];
        
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(c), &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(c), scale, scale, &x0, &y0, &x1, &y1);

        int w = x1 - x0;
        int h = y1 - y0;

        if (w > 0 && h > 0) {
            std::vector<unsigned char> bitmap(static_cast<size_t>(w * h));
            stbtt_MakeCodepointBitmap(&font, bitmap.data(), w, h, w, scale, scale, static_cast<int>(c));

            int draw_x_start = static_cast<int>(std::floor(cur_x + static_cast<float>(x0)));
            int draw_y_start = static_cast<int>(std::floor(baseline_y + static_cast<float>(y0)));

            for (int by = 0; by < h; ++by) {
                int py = draw_y_start + by;
                if (py < 0 || py >= framebuffer.height()) continue;

                for (int bx = 0; bx < w; ++bx) {
                    int px = draw_x_start + bx;
                    if (px < 0 || px >= framebuffer.width()) continue;

                    float glyph_alpha = static_cast<float>(bitmap[static_cast<size_t>(by * w + bx)]) / 255.0f;
                    if (glyph_alpha <= 0.0f) continue;

                    Color src = t.style.color;
                    src.a *= glyph_alpha;

                    Color dst = framebuffer.get_pixel(px, py);
                    framebuffer.set_pixel(px, py, blend_normal(src, dst));
                }
            }
        }

        cur_x += static_cast<float>(advance) * scale;

        if (i + 1 < t.text.length()) {
            cur_x += static_cast<float>(stbtt_GetCodepointKernAdvance(&font, static_cast<int>(c), static_cast<int>(t.text[i + 1]))) * scale;
        }
    }

    return true;
}

} // namespace chronon3d
