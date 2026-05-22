#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <optional>
#include <stdexcept>

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
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("Framebuffer dimensions must be positive");
        }
        m_pixels.resize(static_cast<size_t>(width) * height, Color::black());
        increment_allocations(width * height * sizeof(Color));
    }

    Framebuffer(const Framebuffer& other)
        : m_width(other.m_width), m_height(other.m_height), m_origin_x(other.m_origin_x), m_origin_y(other.m_origin_y), m_pixels(other.m_pixels), m_opaque(other.m_opaque) {
        increment_allocations(size_bytes());
    }

    Framebuffer(Framebuffer&& other) noexcept
        : m_width(other.m_width), m_height(other.m_height), m_origin_x(other.m_origin_x), m_origin_y(other.m_origin_y), m_pixels(std::move(other.m_pixels)), m_opaque(other.m_opaque) {
        other.m_width = 0;
        other.m_height = 0;
        other.m_origin_x = 0;
        other.m_origin_y = 0;
        other.m_opaque = false;
    }

    Framebuffer& operator=(const Framebuffer& other) {
        if (this != &other) {
            decrement_allocations(size_bytes());
            m_width = other.m_width;
            m_height = other.m_height;
            m_origin_x = other.m_origin_x;
            m_origin_y = other.m_origin_y;
            m_pixels = other.m_pixels;
            m_opaque = other.m_opaque;
            increment_allocations(size_bytes());
        }
        return *this;
    }

    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            decrement_allocations(size_bytes());
            m_width = other.m_width;
            m_height = other.m_height;
            m_origin_x = other.m_origin_x;
            m_origin_y = other.m_origin_y;
            m_pixels = std::move(other.m_pixels);
            m_opaque = other.m_opaque;
            other.m_width = 0;
            other.m_height = 0;
            other.m_origin_x = 0;
            other.m_origin_y = 0;
            other.m_opaque = false;
        }
        return *this;
    }

    ~Framebuffer() {
        decrement_allocations(size_bytes());
    }

    void clear(const Color& color) {
        std::fill(m_pixels.begin(), m_pixels.end(), color);
        m_opaque = color.a >= 0.999f;
    }

    void clear(const Color& color, const std::optional<raster::BBox>& clip) {
        if (!clip) {
            clear(color);
            return;
        }

        raster::BBox box = *clip;
        box.clip_to(m_width, m_height);
        if (box.is_empty()) {
            return;
        }

        for (i32 y = box.y0; y < box.y1; ++y) {
            Color* row = pixels_row(y);
            std::fill(row + box.x0, row + box.x1, color);
        }
        if (!m_opaque || color.a < 0.999f) {
            m_opaque = false;
        }
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

    [[nodiscard]] Color* data() {
        return m_pixels.data();
    }

    [[nodiscard]] const Color* data() const {
        return m_pixels.data();
    }

    [[nodiscard]] usize pixel_count() const {
        return m_pixels.size();
    }

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] i32 origin_x() const { return m_origin_x; }
    [[nodiscard]] i32 origin_y() const { return m_origin_y; }
    void set_origin(i32 x, i32 y) {
        m_origin_x = x;
        m_origin_y = y;
    }
    [[nodiscard]] usize size_bytes() const { return m_pixels.size() * sizeof(Color); }
    [[nodiscard]] bool is_opaque() const { return m_opaque; }
    void set_opaque(bool opaque) { m_opaque = opaque; }
    [[nodiscard]] u64 key_digest() const { return m_key_digest; }
    void set_key_digest(u64 digest) { m_key_digest = digest; }

private:
    u64 m_key_digest{0};
    void increment_allocations(size_t bytes) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_allocations.fetch_add(1, std::memory_order_relaxed);
            uint64_t current = profiling::g_current_counters->framebuffer_bytes_allocated.fetch_add(bytes, std::memory_order_relaxed) + bytes;
            uint64_t peak = profiling::g_current_counters->framebuffer_bytes_peak.load(std::memory_order_relaxed);
            while (current > peak && !profiling::g_current_counters->framebuffer_bytes_peak.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
                // retry
            }
        }
    }

    void decrement_allocations(size_t bytes) {
        if (profiling::g_current_counters) {
            uint64_t prev = profiling::g_current_counters->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
            uint64_t next = (prev > bytes) ? (prev - bytes) : 0;
            while (!profiling::g_current_counters->framebuffer_bytes_allocated.compare_exchange_weak(prev, next, std::memory_order_relaxed)) {
                next = (prev > bytes) ? (prev - bytes) : 0;
            }
        }
    }

    i32 m_width;
    i32 m_height;
    i32 m_origin_x{0};
    i32 m_origin_y{0};
    bool m_opaque{false};
    std::vector<Color> m_pixels;
};

} // namespace chronon3d
