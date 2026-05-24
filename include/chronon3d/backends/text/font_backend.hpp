#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec2.hpp>
#include <string>
#include <memory>
#include <vector>

namespace chronon3d::text {

struct GlyphMetrics {
    float advance{0.0f};
    Vec2  offset{0, 0};
    int   width{0};
    int   height{0};
};

struct GlyphBitmap {
    int width{0};
    int height{0};
    std::unique_ptr<u8[]> pixels; // Alpha 8-bit
};

class FontBackend {
public:
    virtual ~FontBackend() = default;

    virtual bool load_font(const std::string& path) = 0;
    virtual const std::vector<u8>* get_font_data(const std::string& path) = 0;
    virtual float get_char_advance(const std::string& path, char c, float size) = 0;
    virtual std::unique_ptr<GlyphBitmap> render_glyph(const std::string& path, char c, float size, GlyphMetrics& out_metrics) = 0;
    virtual const std::vector<u8>* get_font_data(const std::string& path) const { return nullptr; }
};

} // namespace chronon3d::text
