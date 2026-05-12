#pragma once

#include <chronon3d/math/color.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

namespace chronon3d {

/**
 * High-precision float framebuffer.
 * Stores pixels in linear space by convention.
 */
class Framebuffer {
public:
    Framebuffer(i32 width, i32 height) : m_width(width), m_height(height) {
        m_pixels.resize(width * height, Color::black());
    }

    void clear(const Color& color) {
        std::fill(m_pixels.begin(), m_pixels.end(), color);
    }

    void set_pixel(i32 x, i32 y, const Color& color) {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
        m_pixels[static_cast<usize>(y) * m_width + x] = color;
    }

    [[nodiscard]] Color get_pixel(i32 x, i32 y) const {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return Color::black();
        return m_pixels[static_cast<usize>(y) * m_width + x];
    }

    bool save_ppm(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;

        ofs << "P6\n" << m_width << " " << m_height << "\n255\n";
        for (const auto& c : m_pixels) {
            Color srgb = c.to_srgb();
            ofs.put(static_cast<u8>(std::clamp(srgb.r * 255.0f, 0.0f, 255.0f)));
            ofs.put(static_cast<u8>(std::clamp(srgb.g * 255.0f, 0.0f, 255.0f)));
            ofs.put(static_cast<u8>(std::clamp(srgb.b * 255.0f, 0.0f, 255.0f)));
        }
        return true;
    }

    [[nodiscard]] Color* pixels_row(i32 y) {
        return m_pixels.data() + static_cast<usize>(y) * m_width;
    }
    [[nodiscard]] const Color* pixels_row(i32 y) const {
        return m_pixels.data() + static_cast<usize>(y) * m_width;
    }

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }

private:
    i32 m_width;
    i32 m_height;
    std::vector<Color> m_pixels;
};

} // namespace chronon3d
