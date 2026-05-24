#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/profiling/trace.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/memory/memory_utils.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <atomic>

namespace chronon3d {

namespace profiling {
    inline std::atomic<uint64_t> g_live_framebuffer_bytes{0};
    inline std::atomic<uint64_t> g_peak_live_framebuffer_bytes{0};
}

// High-precision float framebuffer.
enum class SamplingMode {
    Nearest,
    Bilinear
};

class Framebuffer {
public:
    Framebuffer(i32 width, i32 height)
        : m_width(width), m_height(height), m_allocated_width(width), m_allocated_height(height), m_owns_pixels(true) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("Framebuffer dimensions must be positive");
        }
        m_pixels.resize(static_cast<size_t>(width) * height, Color::transparent());
        increment_allocations(width * height * sizeof(Color));
    }

    Framebuffer(i32 width, i32 height, Color* external_pixels)
        : m_width(width), m_height(height), m_allocated_width(width), m_allocated_height(height), m_owns_pixels(false), m_external_pixels(external_pixels) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("Framebuffer dimensions must be positive");
        }
    }

    Framebuffer(const Framebuffer& other)
        : m_width(other.m_width), m_height(other.m_height), m_allocated_width(other.m_allocated_width), m_allocated_height(other.m_allocated_height),
          m_origin_x(other.m_origin_x), m_origin_y(other.m_origin_y), m_opaque(other.m_opaque), m_owns_pixels(other.m_owns_pixels) {
        if (m_owns_pixels) {
            m_pixels = other.m_pixels;
            increment_allocations(size_bytes());
        } else {
            m_external_pixels = other.m_external_pixels;
        }
    }

    Framebuffer(Framebuffer&& other) noexcept
        : m_width(other.m_width), m_height(other.m_height), m_allocated_width(other.m_allocated_width), m_allocated_height(other.m_allocated_height),
          m_origin_x(other.m_origin_x), m_origin_y(other.m_origin_y), m_opaque(other.m_opaque), m_owns_pixels(other.m_owns_pixels) {
        if (m_owns_pixels) {
            m_pixels = std::move(other.m_pixels);
        } else {
            m_external_pixels = other.m_external_pixels;
        }
        other.m_width = 0;
        other.m_height = 0;
        other.m_allocated_width = 0;
        other.m_allocated_height = 0;
        other.m_origin_x = 0;
        other.m_origin_y = 0;
        other.m_opaque = false;
        other.m_owns_pixels = true;
    }

    Framebuffer& operator=(const Framebuffer& other) {
        if (this != &other) {
            if (m_owns_pixels) decrement_allocations(size_bytes());
            m_width = other.m_width;
            m_height = other.m_height;
            m_allocated_width = other.m_allocated_width;
            m_allocated_height = other.m_allocated_height;
            m_origin_x = other.m_origin_x;
            m_origin_y = other.m_origin_y;
            m_opaque = other.m_opaque;
            m_owns_pixels = other.m_owns_pixels;
            if (m_owns_pixels) {
                m_pixels = other.m_pixels;
                increment_allocations(size_bytes());
            } else {
                m_external_pixels = other.m_external_pixels;
            }
        }
        return *this;
    }

    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            if (m_owns_pixels) decrement_allocations(size_bytes());
            m_width = other.m_width;
            m_height = other.m_height;
            m_allocated_width = other.m_allocated_width;
            m_allocated_height = other.m_allocated_height;
            m_origin_x = other.m_origin_x;
            m_origin_y = other.m_origin_y;
            m_opaque = other.m_opaque;
            m_owns_pixels = other.m_owns_pixels;
            if (m_owns_pixels) {
                m_pixels = std::move(other.m_pixels);
            } else {
                m_external_pixels = other.m_external_pixels;
            }
            other.m_width = 0;
            other.m_height = 0;
            other.m_allocated_width = 0;
            other.m_allocated_height = 0;
            other.m_origin_x = 0;
            other.m_origin_y = 0;
            other.m_opaque = false;
            other.m_owns_pixels = true;
        }
        return *this;
    }

    ~Framebuffer() {
        if (m_owns_pixels) decrement_allocations(size_bytes());
    }

    void clear(const Color& color) {
        Color* p = data();
        const size_t n = pixel_count();
        if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f) {
            std::memset(p, 0, n * sizeof(Color));
        } else {
            std::fill(p, p + n, color);
        }
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

        if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f) {
            const size_t row_bytes = static_cast<size_t>(box.x1 - box.x0) * sizeof(Color);
            for (i32 y = box.y0; y < box.y1; ++y) {
                Color* row = pixels_row(y);
                std::memset(row + box.x0, 0, row_bytes);
            }
        } else {
            for (i32 y = box.y0; y < box.y1; ++y) {
                Color* row = pixels_row(y);
                std::fill(row + box.x0, row + box.x1, color);
            }
        }
        if (!m_opaque || color.a < 0.999f) {
            m_opaque = false;
        }
    }

    void set_pixel(i32 x, i32 y, const Color& color) {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
        data()[static_cast<usize>(y) * m_allocated_width + x] = color;
    }

    [[nodiscard]] Color get_pixel(i32 x, i32 y) const {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return Color::transparent();
        return data()[static_cast<usize>(y) * m_allocated_width + x];
    }

    [[nodiscard]] Color sample(f32 x, f32 y, SamplingMode mode = SamplingMode::Nearest) const {
        if (mode == SamplingMode::Nearest) return sample_nearest(x, y);
        return sample_bilinear(x, y);
    }

    [[nodiscard]] Color sample_nearest(f32 x, f32 y) const {
        return get_pixel(static_cast<i32>(std::floor(x)), static_cast<i32>(std::floor(y)));
    }

    [[nodiscard]] Color sample_bilinear(f32 x, f32 y) const {
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
        for (i32 y = 0; y < m_height; ++y) {
            const Color* row = pixels_row(y);
            for (i32 x = 0; x < m_width; ++x) {
                ofs.put(Color::linear_to_srgb8(row[x].r));
                ofs.put(Color::linear_to_srgb8(row[x].g));
                ofs.put(Color::linear_to_srgb8(row[x].b));
            }
        }
        return true;
    }

    [[nodiscard]] Color* pixels_row(i32 y) {
        return data() + static_cast<usize>(y) * m_allocated_width;
    }
    [[nodiscard]] const Color* pixels_row(i32 y) const {
        return data() + static_cast<usize>(y) * m_allocated_width;
    }

    [[nodiscard]] Color* data() {
        return m_owns_pixels ? m_pixels.data() : m_external_pixels;
    }

    [[nodiscard]] const Color* data() const {
        return m_owns_pixels ? m_pixels.data() : m_external_pixels;
    }

    [[nodiscard]] usize pixel_count() const {
        return static_cast<size_t>(m_width) * m_height;
    }

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] i32 allocated_width() const { return m_allocated_width; }
    [[nodiscard]] i32 stride() const { return m_allocated_width; }
    [[nodiscard]] i32 allocated_height() const { return m_allocated_height; }
    [[nodiscard]] i32 origin_x() const { return m_origin_x; }
    [[nodiscard]] i32 origin_y() const { return m_origin_y; }
    void set_origin(i32 x, i32 y) { m_origin_x = x; m_origin_y = y; }
    [[nodiscard]] usize size_bytes() const { return static_cast<size_t>(m_allocated_width) * m_allocated_height * sizeof(Color); }
    [[nodiscard]] bool is_opaque() const { return m_opaque; }
    void set_opaque(bool opaque) { m_opaque = opaque; }
    [[nodiscard]] u64 key_digest() const { return m_key_digest; }
    void set_key_digest(u64 digest) { m_key_digest = digest; }
    [[nodiscard]] bool is_arena_allocated() const { return !m_owns_pixels && m_external_pixels != nullptr; }

    /**
     * @brief Shifts the content of the framebuffer by (dx, dy) pixels.
     * Uses memmove for efficiency. Empty areas are NOT cleared.
     */
    void shift(i32 dx, i32 dy) {
        if (dx == 0 && dy == 0) return;
        if (std::abs(dx) >= m_width || std::abs(dy) >= m_height) {
            // Full shift, essentially empty
            return;
        }

        const i32 row_bytes = (m_width - std::abs(dx)) * sizeof(Color);
        const i32 src_x = std::max(0, -dx);
        const i32 dst_x = std::max(0, dx);
        const i32 rows_to_copy = m_height - std::abs(dy);

        if (dy >= 0) {
            // Shift down, process rows from bottom to top
            for (i32 y = rows_to_copy - 1; y >= 0; --y) {
                const Color* src_row = pixels_row(y) + src_x;
                Color* dst_row = pixels_row(y + dy) + dst_x;
                std::memmove(dst_row, src_row, row_bytes);
            }
        } else {
            // Shift up, process rows from top to bottom
            for (i32 y = 0; y < rows_to_copy; ++y) {
                const Color* src_row = pixels_row(y - dy) + src_x;
                Color* dst_row = pixels_row(y) + dst_x;
                std::memmove(dst_row, src_row, row_bytes);
            }
        }
    }

    void resize_logical(i32 width, i32 height) {
        if (width <= 0 || height <= 0) throw std::invalid_argument("Resize dimensions must be positive");
        if (!m_owns_pixels) {
            if (width * height > m_allocated_width * m_allocated_height) {
                throw std::runtime_error("Cannot resize external Framebuffer beyond its allocated size");
            }
            m_width = width;
            m_height = height;
            return;
        }
        const size_t prev_bytes = size_bytes();
        m_width = width;
        m_height = height;
        m_pixels.resize(static_cast<size_t>(width) * height, Color::transparent());
        const size_t next_bytes = size_bytes();
        if (next_bytes > prev_bytes) increment_allocations(next_bytes - prev_bytes);
        else if (next_bytes < prev_bytes) decrement_allocations(prev_bytes - next_bytes);
    }

private:
    u64 m_key_digest{0};
    void increment_allocations(size_t bytes) {
        uint64_t current = profiling::g_live_framebuffer_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        uint64_t peak = profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed);
        while (current > peak && !profiling::g_peak_live_framebuffer_bytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {}
        if (profiling::g_current_counters) profiling::g_current_counters->framebuffer_allocations.fetch_add(1, std::memory_order_relaxed);
    }

    void decrement_allocations(size_t bytes) {
        uint64_t prev = profiling::g_live_framebuffer_bytes.load(std::memory_order_relaxed);
        uint64_t next = (prev > bytes) ? (prev - bytes) : 0;
        while (!profiling::g_live_framebuffer_bytes.compare_exchange_weak(prev, next, std::memory_order_relaxed)) {
            next = (prev > bytes) ? (prev - bytes) : 0;
        }
    }

    i32 m_width;
    i32 m_height;
    i32 m_allocated_width{0};
    i32 m_allocated_height{0};
    i32 m_origin_x{0};
    i32 m_origin_y{0};
    bool m_opaque{false};
    bool m_owns_pixels{true};
    std::vector<Color, memory::HugePageAllocator<Color>> m_pixels;
    Color* m_external_pixels{nullptr};
};

} // namespace chronon3d
