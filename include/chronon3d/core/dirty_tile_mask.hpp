#pragma once

#include <chronon3d/core/tile_grid.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace chronon3d::raster {

// ── DirtyTileMask ────────────────────────────────────────────────────────────
//
// Tracks which tiles in a TileGrid are dirty.  Uses a bit-packed
// std::vector<uint64_t> (NOT std::vector<bool>) for efficient marking and
// iteration.  The mask is always sized to the tile_count() of the grid it
// was constructed with.

class DirtyTileMask {
public:
    DirtyTileMask() = default;

    explicit DirtyTileMask(const TileGrid& grid)
        : m_tile_count(grid.tile_count())
        , m_cols(grid.cols())
    {
        const size_t words = (static_cast<size_t>(m_tile_count) + 63) / 64;
        m_bits.assign(words, 0);
    }

    // ── Marking ──────────────────────────────────────────────────────────

    void clear() {
        std::fill(m_bits.begin(), m_bits.end(), 0);
    }

    void mark_tile(int tx, int ty) {
        const int idx = ty * m_cols + tx;
        if (idx < 0 || idx >= m_tile_count) return;
        const size_t word = static_cast<size_t>(idx) / 64;
        const size_t bit  = static_cast<size_t>(idx) % 64;
        m_bits[word] |= (uint64_t{1} << bit);
    }

    void mark_bbox(const TileGrid& grid, const BBox& bbox) {
        const TileRect tr = grid.tiles_for_bbox(bbox);
        for (int ty = tr.y0; ty < tr.y1; ++ty) {
            for (int tx = tr.x0; tx < tr.x1; ++tx) {
                mark_tile(tx, ty);
            }
        }
    }

    void mark_all() {
        const size_t full_words = static_cast<size_t>(m_tile_count) / 64;
        for (size_t i = 0; i < full_words; ++i) {
            m_bits[i] = ~uint64_t{0};
        }
        // Partial final word
        const size_t remaining = static_cast<size_t>(m_tile_count) % 64;
        if (remaining > 0) {
            m_bits[full_words] = (uint64_t{1} << remaining) - 1;
        }
    }

    // ── Queries ──────────────────────────────────────────────────────────

    [[nodiscard]] bool is_dirty(int tx, int ty) const {
        const int idx = ty * m_cols + tx;
        if (idx < 0 || idx >= m_tile_count) return false;
        const size_t word = static_cast<size_t>(idx) / 64;
        const size_t bit  = static_cast<size_t>(idx) % 64;
        return (m_bits[word] & (uint64_t{1} << bit)) != 0;
    }

    [[nodiscard]] bool any() const {
        for (uint64_t w : m_bits) {
            if (w != 0) return true;
        }
        return false;
    }

    [[nodiscard]] int dirty_count() const {
        int count = 0;
        for (uint64_t w : m_bits) {
            count += __builtin_popcountll(w);
        }
        return count;
    }

    // ── Iteration ────────────────────────────────────────────────────────

    template <typename Fn>
    void for_each_dirty_tile(const TileGrid& grid, Fn&& fn) const {
        (void)grid; // grid passed for API clarity; m_cols is stored internally
        for (size_t word_idx = 0; word_idx < m_bits.size(); ++word_idx) {
            uint64_t w = m_bits[word_idx];
            while (w != 0) {
                const int bit = __builtin_ctzll(w);
                const int idx = static_cast<int>(word_idx * 64 + bit);
                if (idx >= m_tile_count) break; // partial final word guard
                const int tx = idx % m_cols;
                const int ty = idx / m_cols;
                fn(tx, ty);
                w &= w - 1; // clear lowest set bit
            }
        }
    }

    // ── Bit vector access (for serialization / debug) ────────────────────

    [[nodiscard]] const std::vector<uint64_t>& bits() const { return m_bits; }
    [[nodiscard]] int tile_count() const { return m_tile_count; }

private:
    int m_tile_count{0};
    int m_cols{0};
    std::vector<uint64_t> m_bits;
};

} // namespace chronon3d::raster
