#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include "../software/utils/blend2d_resources.hpp"
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
    // Content-only text atlas key:
    // keep raster-relevant typography fields, but do not include placement or runtime state.
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

    // V2 Paint
    seed = hash_combine(seed, hash_value(t.style.paint.fill.r));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.g));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.b));
    seed = hash_combine(seed, hash_value(t.style.paint.fill.a));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_enabled));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.r));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.g));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.b));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_color.a));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_width));

    // V2 BoxStyle
    seed = hash_combine(seed, hash_value(t.style.box_style.enabled));
    seed = hash_combine(seed, hash_value(t.style.box_style.padding.x));
    seed = hash_combine(seed, hash_value(t.style.box_style.padding.y));
    seed = hash_combine(seed, hash_value(t.style.box_style.radius));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.r));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.g));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.b));
    seed = hash_combine(seed, hash_value(t.style.box_style.background.a));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_enabled));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.r));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.g));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.b));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_color.a));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_width));

    // V2 VerticalAlign
    seed = hash_combine(seed, hash_value(static_cast<int>(t.style.vertical_align)));

    // V2 Shadows
    seed = hash_combine(seed, hash_value(t.style.shadows.size()));
    for (const auto& shadow : t.style.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_value(shadow.offset.x));
        seed = hash_combine(seed, hash_value(shadow.offset.y));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_value(shadow.color.r));
        seed = hash_combine(seed, hash_value(shadow.color.g));
        seed = hash_combine(seed, hash_value(shadow.color.b));
        seed = hash_combine(seed, hash_value(shadow.color.a));
    }

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
    int padding,
    bool* cache_hit
) {
    std::string font_path = t.style.font_path;
    if (t.text.empty() || font_path.empty()) return std::nullopt;

    const CacheKey key = hash_text_style(t, effective_size, padding);
    {
        std::lock_guard<std::mutex> lock(g_text_raster_cache_mutex);
        auto it = g_text_raster_cache.find(key);
        if (it != g_text_raster_cache.end()) {
            if (cache_hit) {
                *cache_hit = true;
            }
            return it->second.raster;
        }
    }

    if (cache_hit) {
        *cache_hit = false;
    }

    BLFontFace face = blend2d_utils::Blend2DResources::instance().get_face(font_path);
    if (face.empty()) return std::nullopt;

    BLFont font;
    font.createFromFace(face, effective_size);

    // Setup custom char_width measuring using Blend2D's loaded font
    auto cw = [&](char c, float sz) -> float {
        BLFont measure_font;
        measure_font.createFromFace(face, sz);
        BLGlyphBuffer gb;
        char buf[2] = {c, '\0'};
        gb.setUtf8Text(buf, 1);
        measure_font.shape(gb);
        BLTextMetrics m;
        measure_font.getTextMetrics(gb, m);
        return static_cast<float>(m.advance.x);
    };

    // Account for TextBoxStyle padding if enabled
    TextBox layout_box = t.box;
    if (t.style.box_style.enabled && t.box.enabled) {
        layout_box.size.x = std::max(0.0f, t.box.size.x - 2.0f * t.style.box_style.padding.x);
        layout_box.size.y = std::max(0.0f, t.box.size.y - 2.0f * t.style.box_style.padding.y);
    }

    // Run the multi-line layout engine
    TextLayoutInput layout_in;
    layout_in.text = t.text;
    layout_in.style = t.style;
    layout_in.style.size = effective_size;
    layout_in.box = layout_box;
    layout_in.char_width = cw;

    auto layout_res = TextLayoutEngine::layout(layout_in);

    // Calculate dimensions
    int tw = 0;
    int th = 0;
    if (t.box.enabled) {
        tw = static_cast<int>(std::ceil(t.box.size.x)) + padding;
        th = static_cast<int>(std::ceil(t.box.size.y)) + padding;
    } else {
        float box_w = layout_res.size.x;
        float box_h = layout_res.size.y;
        if (t.style.box_style.enabled) {
            box_w += 2.0f * t.style.box_style.padding.x;
            box_h += 2.0f * t.style.box_style.padding.y;
        }
        tw = static_cast<int>(std::ceil(box_w)) + padding;
        th = static_cast<int>(std::ceil(box_h)) + padding;
    }

    if (tw <= 0 || th <= 0) return std::nullopt;

    BLImage img(tw, th, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.clearAll();

    // Render TextBox background card if enabled
    if (t.style.box_style.enabled) {
        float rect_w = t.box.enabled ? t.box.size.x : (layout_res.size.x + 2.0f * t.style.box_style.padding.x);
        float rect_h = t.box.enabled ? t.box.size.y : (layout_res.size.y + 2.0f * t.style.box_style.padding.y);
        BLRoundRect rect(padding / 2.0f, padding / 2.0f, rect_w, rect_h, t.style.box_style.radius, t.style.box_style.radius);

        ctx.setFillStyle(BLRgba32(
            static_cast<uint8_t>(std::clamp(t.style.box_style.background.r * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(t.style.box_style.background.g * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(t.style.box_style.background.b * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(t.style.box_style.background.a * 255.0f, 0.0f, 255.0f))
        ));
        ctx.fillRoundRect(rect);

        if (t.style.box_style.border_enabled && t.style.box_style.border_width > 0.0f) {
            ctx.setStrokeWidth(t.style.box_style.border_width);
            ctx.setStrokeStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(t.style.box_style.border_color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.box_style.border_color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.box_style.border_color.b * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.box_style.border_color.a * 255.0f, 0.0f, 255.0f))
            ));
            ctx.strokeRoundRect(rect);
        }
    }

    // Calculate vertical alignment offset
    float free_w = t.box.enabled ? (t.box.size.x - (t.style.box_style.enabled ? 2.0f * t.style.box_style.padding.x : 0.0f)) : 0.0f;
    float free_h = t.box.enabled ? (t.box.size.y - (t.style.box_style.enabled ? 2.0f * t.style.box_style.padding.y : 0.0f)) : 0.0f;
    float dx_align = 0.0f;
    float dy_align = 0.0f;
    if (t.box.enabled && free_w > layout_res.size.x) {
        if (t.style.align == TextAlign::Center) {
            dx_align = (free_w - layout_res.size.x) * 0.5f;
        } else if (t.style.align == TextAlign::Right) {
            dx_align = free_w - layout_res.size.x;
        }
    }
    if (t.box.enabled && free_h > layout_res.size.y) {
        if (t.style.vertical_align == VerticalAlign::Middle) {
            dy_align = (free_h - layout_res.size.y) * 0.5f;
        } else if (t.style.vertical_align == VerticalAlign::Bottom) {
            dy_align = free_h - layout_res.size.y;
        }
    }

    // Text starting position offset
    float text_start_x = padding / 2.0f + (t.style.box_style.enabled ? t.style.box_style.padding.x : 0.0f) + dx_align;
    float text_start_y = padding / 2.0f + (t.style.box_style.enabled ? t.style.box_style.padding.y : 0.0f) + dy_align;

    // Resolve color
    Color fill_color = t.style.paint.fill;
    if (fill_color == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(t.style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
        fill_color = t.style.color;
    }

    // Draw lines
    for (const auto& line : layout_res.lines) {
        if (line.text.empty()) continue;

        float lx = text_start_x + line.position.x;
        float ly = text_start_y + line.position.y + font.metrics().ascent;

        if (t.style.paint.stroke_enabled && t.style.paint.stroke_width > 0.0f) {
            ctx.setStrokeWidth(t.style.paint.stroke_width);
            ctx.setStrokeStyle(BLRgba32(
                static_cast<uint8_t>(std::clamp(t.style.paint.stroke_color.r * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.paint.stroke_color.g * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.paint.stroke_color.b * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(t.style.paint.stroke_color.a * 255.0f, 0.0f, 255.0f))
            ));
            ctx.strokeUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
        }

        ctx.setFillStyle(BLRgba32(
            static_cast<uint8_t>(std::clamp(fill_color.r * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(fill_color.g * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(fill_color.b * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(fill_color.a * 255.0f, 0.0f, 255.0f))
        ));
        ctx.fillUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
    }

    ctx.end();

    // Calculate layout metrics bounding box
    BLGlyphBuffer gb;
    gb.setUtf8Text(t.text.c_str(), t.text.size());
    font.shape(gb);
    BLTextMetrics metrics;
    font.getTextMetrics(gb, metrics);

    // Compute external offsets
    float x_offset = 0.0f;
    if (t.box.enabled) {
        x_offset = -padding / 2.0f;
    } else {
        const float full_width = layout_res.size.x;
        if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
        else if (t.style.align == TextAlign::Right) x_offset = -full_width;
        x_offset += metrics.boundingBox.x0 - (padding / 2.0f);
    }

    float y_offset = 0.0f;
    if (t.box.enabled) {
        y_offset = -padding / 2.0f;
    } else {
        y_offset = -font.metrics().ascent - (padding / 2.0f);
        if (t.style.align == TextAlign::Center) {
            y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
        }
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
