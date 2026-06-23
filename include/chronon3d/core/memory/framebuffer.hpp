#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/memory/memory_utils.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <atomic>
#include <new>
#include <span>

namespace chronon3d {

namespace profiling {
    inline std::atomic<uint64_t> g_live_framebuffer_bytes{0};
    inline std::atomic<uint64_t> g_peak_live_framebuffer_bytes{0};
}

#include "detail/framebuffer_impl.hpp"

// High-precision float framebuffer.
enum class SamplingMode {
    Nearest,
    Bilinear
};

// Cache-line alignment: ensures rows don't share cache lines between threads.
// Hardcoded to 64 bytes as a safe portable value for all major architectures.
// (Apple Silicon uses 128B but 64B alignment still prevents false sharing).
constexpr size_t k_cache_line_bytes = 64;
constexpr size_t k_color_size = sizeof(Color);  // 16 bytes (4 × float)
static_assert(k_cache_line_bytes % k_color_size == 0,
              "Cache line size must be a multiple of Color size");
constexpr size_t k_colors_per_cache_line = k_cache_line_bytes / k_color_size;  // 4

// Round up stride to cache-line boundary for SIMD-friendly access.
[[nodiscard]] inline i32 align_stride_to_cache_line(i32 width) {
    if (width <= 0) return 0;
    const i32 remainder = width % static_cast<i32>(k_colors_per_cache_line);
    if (remainder == 0) return width;
    return width + static_cast<i32>(k_colors_per_cache_line) - remainder;
}

class Framebuffer {
public:
    Framebuffer(i32 width, i32 height)
        : m_width(width), m_height(height), m_allocated_width(align_stride_to_cache_line(width)),
          m_allocated_height(height), m_owns_pixels(true), m_content_cleared{true} {
        framebuffer_validate_dimensions(width, height);
        m_pixels.resize(static_cast<size_t>(m_allocated_width) * m_allocated_height, Color::transparent());
        framebuffer_increment_allocations(size_bytes());
    }

    // TICKET-011-final — constructors required by callers that haven't
    // been migrated. Provide both (int,int,bool) (int&,int&,bool&) overloads.
    // The bool parameter is "initial clear" — when true the framebuffer
    // already starts cleared (default behaviour); when false the caller
    // promises to overwrite every pixel and we skip the zero-init step.
    Framebuffer(i32 width, i32 height, bool initial_clear)
        : m_width(width), m_height(height), m_allocated_width(align_stride_to_cache_line(width)),
          m_allocated_height(height), m_owns_pixels(true),
          m_content_cleared{initial_clear} {
        framebuffer_validate_dimensions(width, height);
        if (initial_clear) {
            m_pixels.resize(static_cast<size_t>(m_allocated_width) * m_allocated_height, Color::transparent());
        } else {
            m_pixels.resize(static_cast<size_t>(m_allocated_width) * m_allocated_height);
        }
        framebuffer_increment_allocations(size_bytes());
    }

    Framebuffer(i32 width, i32 height, Color* external_pixels)
        : m_width(width), m_height(height),
          m_allocated_width(align_stride_to_cache_line(width)),
          m_allocated_height(height), m_owns_pixels(false), m_content_cleared{false},
          m_external_pixels(external_pixels) {
        framebuffer_validate_dimensions(width, height);
    }

    Framebuffer(const Framebuffer& other)
        : m_width(other.m_width), m_height(other.m_height), m_allocated_width(other.m_allocated_width),
          m_allocated_height(other.m_allocated_height), m_origin_x(other.m_origin_x),
          m_origin_y(other.m_origin_y), m_opaque(other.m_opaque), m_owns_pixels(other.m_owns_pixels),
          m_content_cleared(other.m_content_cleared), m_key_digest(other.m_key_digest) {
        copy_pixels_from(other);
    }

    Framebuffer(Framebuffer&& other) noexcept
        : m_width(other.m_width), m_height(other.m_height), m_allocated_width(other.m_allocated_width),
          m_allocated_height(other.m_allocated_height), m_origin_x(other.m_origin_x),
          m_origin_y(other.m_origin_y), m_opaque(other.m_opaque), m_owns_pixels(other.m_owns_pixels),
          m_content_cleared(other.m_content_cleared), m_key_digest(other.m_key_digest) {
        move_pixels_from(std::move(other));
    }

    Framebuffer& operator=(const Framebuffer& other) {
        if (this != &other) {
            release_owned_pixels();
            copy_metadata_from(other);
            copy_pixels_from(other);
        }
        return *this;
    }

    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            release_owned_pixels();
            copy_metadata_from(other);
            move_pixels_from(std::move(other));
        }
        return *this;
    }

    ~Framebuffer() {
        if (m_owns_pixels) framebuffer_decrement_allocations(size_bytes());
    }

    void clear(const Color& color) {
        Color* ptr = m_owns_pixels ? m_pixels.data() : m_external_pixels;
        if (m_allocated_width == m_width) {
            framebuffer_clear_contiguous(ptr, pixel_count(), color);
        } else {
            framebuffer_clear_strided(ptr, m_allocated_width, 0, 0, m_width, m_height, color);
        }
        m_opaque = color.a >= 0.999f;
        // Only claim "content cleared" when the color is actually transparent.
        m_content_cleared =
            (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f);
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

        if (box.x0 == 0 && box.y0 == 0 && box.x1 == m_width && box.y1 == m_height) {
            clear(color);
            return;
        }

        Color* ptr = m_owns_pixels ? m_pixels.data() : m_external_pixels;
        framebuffer_clear_strided(ptr, m_allocated_width, box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0, color);
        if (!m_opaque || color.a < 0.999f) {
            m_opaque = false;
        }
        // Partial clear does not guarantee the entire framebuffer is transparent.
        m_content_cleared = false;
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

    // TICKET-035 — deterministic byte view of the logical pixel storage.
    // Used by parity assertions (e.g. TC007 in test_motion_blur_torture_pr1.cpp).
    // Returns a span over the active logical area only (m_width * m_height, no
    // stride padding) reinterpreted as std::byte.  Row-major, contiguous within
    // each row; callers can hash the bytes directly for bit-exact comparisons.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

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

    bool save_ppm(const std::string& path) const;

    [[nodiscard]] Color* pixels_row(i32 y) {
        // data() already invalidates content_cleared; no need to do it twice.
        return data() + static_cast<usize>(y) * m_allocated_width;
    }
    [[nodiscard]] const Color* pixels_row(i32 y) const {
        return data() + static_cast<usize>(y) * m_allocated_width;
    }

    [[nodiscard]] Color* data() {
        m_content_cleared = false;
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
    [[nodiscard]] bool is_content_cleared() const {
        return m_content_cleared;
    }
    void set_content_cleared(bool cleared) {
        m_content_cleared = cleared;
    }
    [[nodiscard]] u64 key_digest() const { return m_key_digest; }
    void set_key_digest(u64 digest) { m_key_digest = digest; }
    [[nodiscard]] bool is_arena_allocated() const { return !m_owns_pixels && m_external_pixels != nullptr; }

    /**
     * @brief Swap the full contents (pixel data + metadata) with another framebuffer.
     *
     * No pixel data is copied — vectors and pointers are swapped. The total
     * tracked live bytes remain unchanged. After the swap, @p other holds
     * whatever this framebuffer held, and vice-versa.
     */
    void swap_contents(Framebuffer& other) noexcept {
        using std::swap;
        swap(m_width,            other.m_width);
        swap(m_height,           other.m_height);
        swap(m_allocated_width,  other.m_allocated_width);
        swap(m_allocated_height, other.m_allocated_height);
        swap(m_origin_x,         other.m_origin_x);
        swap(m_origin_y,         other.m_origin_y);
        swap(m_opaque,           other.m_opaque);
        swap(m_content_cleared,  other.m_content_cleared);
        swap(m_key_digest,       other.m_key_digest);
        swap(m_owns_pixels,      other.m_owns_pixels);
        swap(m_pixels,           other.m_pixels);
        swap(m_external_pixels,  other.m_external_pixels);
    }

    /**
     * @brief Shifts the content of the framebuffer by (dx, dy) pixels.
     * Uses memmove for efficiency. Empty areas are NOT cleared.
     */
    void shift(i32 dx, i32 dy);

    /**
     * @brief Copy pixels from @p src into this framebuffer at offset (dst_x, dst_y).
     *
     * Pixels outside this framebuffer's bounds are clipped. This is the
     * canonical way to composite one framebuffer onto another and replaces
     * ad-hoc copy_framebuffer_pixels() helpers.
     */
    void blit(const Framebuffer& src, i32 dst_x, i32 dst_y);

    void resize_logical(i32 width, i32 height);

private:
    u64 m_key_digest{0};

    friend void swap(Framebuffer& a, Framebuffer& b) noexcept {
        a.swap_contents(b);
    }

    void release_owned_pixels() {
        if (m_owns_pixels && !m_pixels.empty()) {
            framebuffer_decrement_allocations(size_bytes());
            m_pixels.clear();
        }
    }

    void copy_metadata_from(const Framebuffer& other) {
        m_width = other.m_width;
        m_height = other.m_height;
        m_allocated_width = other.m_allocated_width;
        m_allocated_height = other.m_allocated_height;
        m_origin_x = other.m_origin_x;
        m_origin_y = other.m_origin_y;
        m_opaque = other.m_opaque;
        m_owns_pixels = other.m_owns_pixels;
        m_content_cleared = other.m_content_cleared;
        m_key_digest = other.m_key_digest;
    }

    void copy_pixels_from(const Framebuffer& other) {
        if (m_owns_pixels) {
            m_pixels = other.m_pixels;
            framebuffer_increment_allocations(size_bytes());
            m_external_pixels = nullptr;
        } else {
            m_external_pixels = other.m_external_pixels;
        }
    }

    void move_pixels_from(Framebuffer&& other) {
        if (m_owns_pixels) {
            m_pixels = std::move(other.m_pixels);
            other.m_pixels.clear();
            m_external_pixels = nullptr;
        } else {
            m_external_pixels = other.m_external_pixels;
        }
        other.reset_transient_state();
    }

    void reset_transient_state() {
        m_width = 0;
        m_height = 0;
        m_allocated_width = 0;
        m_allocated_height = 0;
        m_origin_x = 0;
        m_origin_y = 0;
        m_opaque = false;
        m_owns_pixels = true;
        m_external_pixels = nullptr;
        m_key_digest = 0;
        m_content_cleared = false;
    }

    i32 m_width{0};
    i32 m_height{0};
    i32 m_allocated_width{0};
    i32 m_allocated_height{0};
    i32 m_origin_x{0};
    i32 m_origin_y{0};
    bool m_opaque{false};
    // Invariant: never mutated via data()/clear() when shared across threads.
    // state.shared_transparent is assigned (shared_ptr copy) to multiple
    // state.temp[id] slots in parallel but never modified — if that ever
    // changes, this must revert to std::atomic<bool>.
    bool m_content_cleared{false};
    bool m_owns_pixels{true};
    std::vector<Color, memory::HugePageAllocator<Color>> m_pixels;
    Color* m_external_pixels{nullptr};
};

} // namespace chronon3d
