#pragma once

#include <chronon3d/math/color.hpp>
#include <vector>
#include <string>
#include <fstream>

namespace chronon3d {

class Framebuffer {
public:
    Framebuffer(i32 width, i32 height) : m_width(width), m_height(height) {
        m_pixels.resize(width * height * 4, 0);
    }

    void clear(const Color& color) {
        u8 r = static_cast<u8>(color.r * 255.0f);
        u8 g = static_cast<u8>(color.g * 255.0f);
        u8 b = static_cast<u8>(color.b * 255.0f);
        u8 a = static_cast<u8>(color.a * 255.0f);

        for (usize i = 0; i < m_pixels.size(); i += 4) {
            m_pixels[i] = r;
            m_pixels[i+1] = g;
            m_pixels[i+2] = b;
            m_pixels[i+3] = a;
        }
    }

    void set_pixel(i32 x, i32 y, const Color& color) {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
        
        usize idx = (static_cast<usize>(y) * m_width + x) * 4;
        m_pixels[idx] = static_cast<u8>(color.r * 255.0f);
        m_pixels[idx+1] = static_cast<u8>(color.g * 255.0f);
        m_pixels[idx+2] = static_cast<u8>(color.b * 255.0f);
        m_pixels[idx+3] = static_cast<u8>(color.a * 255.0f);
    }

    [[nodiscard]] Color get_pixel(i32 x, i32 y) const {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return Color::black();
        
        usize idx = (static_cast<usize>(y) * m_width + x) * 4;
        return {
            m_pixels[idx] / 255.0f,
            m_pixels[idx+1] / 255.0f,
            m_pixels[idx+2] / 255.0f,
            m_pixels[idx+3] / 255.0f
        };
    }

    bool save_ppm(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;

        ofs << "P6\n" << m_width << " " << m_height << "\n255\n";
        for (usize i = 0; i < m_pixels.size(); i += 4) {
            ofs.put(m_pixels[i]);   // R
            ofs.put(m_pixels[i+1]); // G
            ofs.put(m_pixels[i+2]); // B
        }
        return true;
    }

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }

private:
    i32 m_width;
    i32 m_height;
    std::vector<u8> m_pixels;
};

} // namespace chronon3d
