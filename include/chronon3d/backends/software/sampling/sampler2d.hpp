#pragma once

// ── Sampler2D ───────────────────────────────────────────────────────────────
//
// Reusable texture sampler that wraps a Framebuffer and applies an EdgeMode.
// Provides nearest and bilinear filtering with configurable edge behaviour.
//
// Coordinate convention (matches Framebuffer::sample_bilinear):
//   Pixel centre (0,0) → coordinate (0.5, 0.5)

#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

namespace chronon3d::sampling {

class Sampler2D {
public:
    /// Construct a sampler around a source framebuffer.
    /// @param source  The framebuffer to sample from. Must outlive the sampler.
    /// @param edge    Edge mode for out-of-bounds coordinates.
    explicit Sampler2D(const Framebuffer& source,
                       EdgeMode edge = EdgeMode::Transparent) noexcept
        : m_source(&source)
        , m_edge(edge)
        , m_width(source.width())
        , m_height(source.height())
        , m_stride(source.stride())
    {}

    Sampler2D() = default;

    /// Change the edge mode after construction.
    void set_edge_mode(EdgeMode mode) noexcept { m_edge = mode; }
    [[nodiscard]] EdgeMode edge_mode() const noexcept { return m_edge; }

    /// Nearest-neighbour sample at (x, y) in pixel coordinates.
    [[nodiscard]] Color nearest(float x, float y) const noexcept;

    /// Bilinear sample at (x, y) in pixel coordinates.
    [[nodiscard]] Color bilinear(float x, float y) const noexcept;

private:
    /// Raw fetch at integer pixel coordinates with edge-mode handling.
    [[nodiscard]] Color fetch(int x, int y) const noexcept;

    const Framebuffer* m_source{nullptr};
    EdgeMode           m_edge{EdgeMode::Transparent};
    int                m_width{0};
    int                m_height{0};
    int                m_stride{0};
};

// ── Inline implementation ───────────────────────────────────────────────────

inline Color Sampler2D::fetch(int x, int y) const noexcept {
    if (x >= 0 && x < m_width && y >= 0 && y < m_height) {
        return m_source->get_pixel(x, y);
    }

    switch (m_edge) {
    case EdgeMode::Transparent:
        return Color::transparent();

    case EdgeMode::Clamp:
        return m_source->get_pixel(
            clamp_coord(x, m_width),
            clamp_coord(y, m_height));

    case EdgeMode::Wrap:
        return m_source->get_pixel(
            wrap_coord(x, m_width),
            wrap_coord(y, m_height));

    case EdgeMode::Mirror:
        return m_source->get_pixel(
            mirror_coord(x, m_width),
            mirror_coord(y, m_height));
    }

    return Color::transparent();
}

inline Color Sampler2D::nearest(float x, float y) const noexcept {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    return fetch(ix, iy);
}

inline Color Sampler2D::bilinear(float x, float y) const noexcept {
    // Shift to centre-relative coordinates (matching Framebuffer convention)
    const float u = x - 0.5f;
    const float v = y - 0.5f;

    const int x0 = static_cast<int>(std::floor(u));
    const int y0 = static_cast<int>(std::floor(v));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    const Color c00 = fetch(x0, y0);
    const Color c10 = fetch(x1, y0);
    const Color c01 = fetch(x0, y1);
    const Color c11 = fetch(x1, y1);

    // Premultiplied-alpha bilinear interpolation (no unpremultiply)
    const Color row0 = lerp(c00, c10, tx);
    const Color row1 = lerp(c01, c11, tx);
    return lerp(row0, row1, ty);
}

} // namespace chronon3d::sampling
