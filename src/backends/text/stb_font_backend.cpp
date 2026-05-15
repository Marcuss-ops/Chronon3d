#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <fstream>
#include <cmath>

namespace chronon3d::text {

namespace {
    bool init_font(const std::vector<u8>& data, stbtt_fontinfo& info) {
        return stbtt_InitFont(&info, data.data(), 0) != 0;
    }
}

bool StbFontBackend::load_font(const std::string& path) {
    if (m_fonts.contains(path)) return true;

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    FontData data;
    data.bytes.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.bytes.data()), size);

    stbtt_fontinfo info;
    if (!init_font(data.bytes, info)) return false;

    m_fonts[path] = std::move(data);
    return true;
}

float StbFontBackend::get_char_advance(const std::string& path, char c, float size) {
    auto it = m_fonts.find(path);
    if (it == m_fonts.end()) return 0.0f;

    stbtt_fontinfo info;
    if (!init_font(it->second.bytes, info)) return 0.0f;

    float scale = stbtt_ScaleForPixelHeight(&info, size);
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&info, (int)c, &advance, &lsb);
    return (float)advance * scale;
}

std::unique_ptr<GlyphBitmap> StbFontBackend::render_glyph(const std::string& path, char c, float size, GlyphMetrics& out_metrics) {
    auto it = m_fonts.find(path);
    if (it == m_fonts.end()) return nullptr;

    stbtt_fontinfo info;
    if (!init_font(it->second.bytes, info)) return nullptr;

    float scale = stbtt_ScaleForPixelHeight(&info, size);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&info, (int)c, scale, scale, &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = y1 - y0;

    auto result = std::make_unique<GlyphBitmap>();
    result->width = gw;
    result->height = gh;
    
    if (gw > 0 && gh > 0) {
        result->pixels = std::make_unique<u8[]>(static_cast<size_t>(gw * gh));
        stbtt_MakeCodepointBitmap(&info, result->pixels.get(), gw, gh, gw, scale, scale, (int)c);
    }

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&info, (int)c, &advance, &lsb);
    
    out_metrics.advance = (float)advance * scale;
    out_metrics.offset = Vec2{(float)x0, (float)y0};
    out_metrics.width = gw;
    out_metrics.height = gh;

    return result;
}

} // namespace chronon3d::text
