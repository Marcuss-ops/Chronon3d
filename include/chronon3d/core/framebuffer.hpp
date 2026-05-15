#pragma once

#include <chronon3d/math/color.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

namespace chronon3d {

// High-precision float framebuffer.
// Convention: pixels are stored in LinearSRGB, straight (non-premultiplied) alpha.
// Convertion to sRGB for output happens in save_ppm() / image_writer.
// See include/chronon3d/math/color_space.hpp for the full pipeline.
enum class SamplingMode {
    Nearest,
    Bilinear
};

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
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return Color::transparent();
        return m_pixels[static_cast<usize>(y) * m_width + x];
    }

    [[nodiscard]] Color sample(f32 x, f32 y, SamplingMode mode = SamplingMode::Nearest) const {
        if (mode == SamplingMode::Nearest) return sample_nearest(x, y);
        return sample_bilinear(x, y);
    }

    [[nodiscard]] Color sample_nearest(f32 x, f32 y) const {
        return get_pixel(static_cast<i32>(std::floor(x)), static_cast<i32>(std::floor(y)));
    }

    [[nodiscard]] Color sample_bilinear(f32 x, f32 y) const {
        // Pixel centers are at (0.5, 0.5)
        const f32 u = x - 0.5f;
        const f32 v = y - 0.5f;
        const i32 x0 = static_cast<i32>(std::floor(u));
        const i32 y0 = static_cast<i32>(std::floor(v));
        const i32 x1 = x0 + 1;
        const i32 y1 = y0 + 1;

        const f32 tx = u - static_cast<f32>(x0);
        const f32 ty = v - static_cast<f32>(y0);

        const Color c00 = get_pixel(x0, y0);
        const Color c10 = get_pixel(x1, y0);
        const Color c01 = get_pixel(x0, y1);
        const Color c11 = get_pixel(x1, y1);

        return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
    }

    bool save_ppm(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;

        ofs << "P6\n" << m_width << " " << m_height << "\n255\n";
        for (const auto& c : m_pixels) {
            ofs.put(Color::linear_to_srgb8(c.r));
            ofs.put(Color::linear_to_srgb8(c.g));
            ofs.put(Color::linear_to_srgb8(c.b));
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
    [[nodiscard]] usize size_bytes() const { return m_pixels.size() * sizeof(Color); }

private:

    i32 m_width;
    i32 m_height;
    std::vector<Color> m_pixels;
};

} // namespace chronon3d
