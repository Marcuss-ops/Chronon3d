// ============================================================================
// framebuffer.cpp — heavy Framebuffer methods extracted from the header
// ============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <fstream>
#include <cstring>

namespace chronon3d {

// ── save_ppm ────────────────────────────────────────────────────────────────

bool Framebuffer::save_ppm(const std::string& path) const {
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

// ── shift ───────────────────────────────────────────────────────────────────

void Framebuffer::shift(i32 dx, i32 dy) {
    if (dx == 0 && dy == 0) return;
    if (std::abs(dx) >= m_width || std::abs(dy) >= m_height) {
        clear(Color::transparent());
        return;
    }

    const i32 row_bytes = (m_width - std::abs(dx)) * sizeof(Color);
    const i32 src_x = std::max(0, -dx);
    const i32 dst_x = std::max(0, dx);
    const i32 rows_to_copy = m_height - std::abs(dy);

    if (dy >= 0) {
        for (i32 y = rows_to_copy - 1; y >= 0; --y) {
            const Color* src_row = pixels_row(y) + src_x;
            Color* dst_row = pixels_row(y + dy) + dst_x;
            std::memmove(dst_row, src_row, row_bytes);
        }
        for (i32 y = 0; y < dy && y < m_height; ++y) {
            simd::clear_framebuffer(std::span<Color>(pixels_row(y), m_allocated_width), Color::transparent());
        }
    } else {
        const i32 abs_dy = -dy;
        for (i32 y = 0; y < rows_to_copy; ++y) {
            const Color* src_row = pixels_row(y + abs_dy) + src_x;
            Color* dst_row = pixels_row(y) + dst_x;
            std::memmove(dst_row, src_row, row_bytes);
        }
        for (i32 y = std::max(0, m_height - abs_dy); y < m_height; ++y) {
            simd::clear_framebuffer(std::span<Color>(pixels_row(y), m_allocated_width), Color::transparent());
        }
    }

    if (dx > 0) {
        for (i32 y = 0; y < m_height; ++y) {
            simd::clear_framebuffer(std::span<Color>(pixels_row(y), dx), Color::transparent());
        }
    } else if (dx < 0) {
        const i32 abs_dx = -dx;
        for (i32 y = 0; y < m_height; ++y) {
            simd::clear_framebuffer(std::span<Color>(pixels_row(y) + m_width - abs_dx, abs_dx), Color::transparent());
        }
    }
}

// ── blit ────────────────────────────────────────────────────────────────────

void Framebuffer::blit(const Framebuffer& src, i32 dst_x, i32 dst_y) {
    for (i32 y = 0; y < src.height(); ++y) {
        const i32 dy = dst_y + y;
        if (dy < 0 || dy >= m_height) continue;
        for (i32 x = 0; x < src.width(); ++x) {
            const i32 dx = dst_x + x;
            if (dx < 0 || dx >= m_width) continue;
            data()[static_cast<usize>(dy) * m_allocated_width + dx] =
                src.data()[static_cast<usize>(y) * src.m_allocated_width + x];
        }
    }
}

// ── resize_logical ──────────────────────────────────────────────────────────

void Framebuffer::resize_logical(i32 width, i32 height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("Resize dimensions must be positive");
    if (!m_owns_pixels) {
        if (width > m_allocated_width || height > m_allocated_height) {
            throw std::runtime_error("Cannot resize external Framebuffer beyond its allocated size");
        }
        m_width = width;
        m_height = height;
        return;
    }

    if (width > m_allocated_width || height > m_allocated_height) {
        const size_t prev_bytes = size_bytes();

        m_allocated_width = align_stride_to_cache_line(width);
        m_allocated_height = height;
        m_pixels.resize(static_cast<size_t>(m_allocated_width) * m_allocated_height, Color::transparent());

        const size_t next_bytes = size_bytes();
        if (next_bytes > prev_bytes) framebuffer_increment_allocations(next_bytes - prev_bytes);
        else if (next_bytes < prev_bytes) framebuffer_decrement_allocations(prev_bytes - next_bytes);
    }

    m_width = width;
    m_height = height;
}

} // namespace chronon3d


// ── bytes (TICKET-035) — deterministic byte view of logical pixel storage ──

namespace chronon3d {

std::span<const std::byte> Framebuffer::bytes() const noexcept {
    const std::size_t n = static_cast<std::size_t>(m_width) *
                           static_cast<std::size_t>(m_height) *
                           sizeof(Color);
    return {reinterpret_cast<const std::byte*>(data()), n};
}

}  // namespace chronon3d
