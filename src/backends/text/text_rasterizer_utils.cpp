#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include "../software/utils/blend2d_resources.hpp"
#include <chronon3d/registry/font_registry.hpp>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace chronon3d {

namespace {

using CacheKey = u64;

CacheKey hash_combine(CacheKey seed, CacheKey value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

template <typename T>
CacheKey hash_value(const T& value) {
    return static_cast<CacheKey>(std::hash<T>{}(value));
}

CacheKey hash_text_style(const TextShape& t, float effective_size, int padding) {
    CacheKey seed = 0;
    seed = hash_combine(seed, hash_value(t.text));
    seed = hash_combine(seed, hash_value(t.style.font_path));
    seed = hash_combine(seed, hash_value(t.style.font_family));
    seed = hash_combine(seed, hash_value(t.style.font_weight));
    seed = hash_combine(seed, hash_value(t.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_value(t.style.color.r));
    seed = hash_combine(seed, hash_value(t.style.color.g));
    seed = hash_combine(seed, hash_value(t.style.color.b));
    seed = hash_combine(seed, hash_value(t.style.color.a));
    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.align)));
    seed = hash_combine(seed, hash_value(t.style.line_height));
    seed = hash_combine(seed, hash_value(t.style.tracking));
    seed = hash_combine(seed, hash_value(t.style.max_lines));
    seed = hash_combine(seed, hash_value(t.style.auto_scale));
    seed = hash_combine(seed, hash_value(t.style.min_size));
    seed = hash_combine(seed, hash_value(t.style.max_size));
    seed = hash_combine(seed, hash_value(t.box.size.x));
    seed = hash_combine(seed, hash_value(t.box.size.y));
    seed = hash_combine(seed, hash_value(t.box.enabled));
    seed = hash_combine(seed, hash_value(padding));
    return seed;
}

struct CachedRasterEntry {
    TextRasterization raster;
};

std::unordered_map<CacheKey, CachedRasterEntry> g_text_raster_cache;
std::mutex g_text_raster_cache_mutex;

} // namespace

std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& t,
    float effective_size,
    int padding
) {
    std::string font_path = t.style.font_path;
    if (font_path.empty() && !t.style.font_family.empty()) {
        font_path = FontRegistry::resolve(t.style.font_family, t.style.font_weight, t.style.font_style);
    }
    if (t.text.empty() || font_path.empty()) return std::nullopt;

    const CacheKey key = hash_text_style(t, effective_size, padding);
    {
        std::lock_guard<std::mutex> lock(g_text_raster_cache_mutex);
        auto it = g_text_raster_cache.find(key);
        if (it != g_text_raster_cache.end()) {
            return it->second.raster;
        }
    }

    BLFontFace face = blend2d_utils::Blend2DResources::instance().get_face(font_path);
    if (face.empty()) return std::nullopt;

    BLFont font;
    font.createFromFace(face, effective_size);

    // Calculate bounds using GlyphBuffer
    BLGlyphBuffer gb;
    gb.setUtf8Text(t.text.c_str(), t.text.size());
    font.shape(gb);
    
    BLTextMetrics metrics;
    font.getTextMetrics(gb, metrics);
    
    const int tw = static_cast<int>(std::ceil(metrics.boundingBox.x1 - metrics.boundingBox.x0)) + padding;
    const int th = static_cast<int>(std::ceil(font.metrics().ascent + font.metrics().descent)) + padding;
    
    if (tw <= 0 || th <= 0) return std::nullopt;

    BLImage img(tw, th, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.clearAll();
    
    // We render in pure white (base for tinting or direct use)
    ctx.setFillStyle(BLRgba32(255, 255, 255, 255));
    ctx.fillUtf8Text(BLPoint(-metrics.boundingBox.x0 + (padding/2), font.metrics().ascent + (padding/2)), font, t.text.c_str());
    ctx.end();

    float x_offset = 0.0f;
    const float full_width = metrics.advance.x;
    if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
    else if (t.style.align == TextAlign::Right) x_offset = -full_width;

    // Adjust for the bounding box offset
    x_offset += metrics.boundingBox.x0 - (padding/2);

    float y_offset = -font.metrics().ascent - (padding/2);
    if (t.style.align == TextAlign::Center) {
        // Visually center vertically as well when horizontal centering is used
        y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
    }

    TextRasterization res;
    res.image = std::move(img);
    res.x_offset = x_offset;
    res.y_offset = y_offset;
    res.metrics = metrics;
    res.font = font;

    {
        std::lock_guard<std::mutex> lock(g_text_raster_cache_mutex);
        g_text_raster_cache.emplace(key, CachedRasterEntry{res});
    }

    return res;
}

void clear_text_raster_cache() {
    std::lock_guard<std::mutex> lock(g_text_raster_cache_mutex);
    g_text_raster_cache.clear();
}

} // namespace chronon3d
