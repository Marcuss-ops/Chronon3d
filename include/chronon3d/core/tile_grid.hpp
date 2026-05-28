#pragma once

#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>

namespace chronon3d::raster {

// ── Tile coordinates ─────────────────────────────────────────────────────────

struct TileCoord {
    int x;
    int y;
};

struct TileRect {
    int x0, y0, x1, y1; // tile coords, x1/y1 exclusive
};

// ── TileGrid ─────────────────────────────────────────────────────────────────
//
// Divides a framebuffer of (width × height) into a regular grid of tile_size
// tiles.  Tiles along the right and bottom edges may be smaller than tile_size.
// All bbox inputs are in pixel coordinates (x1/y1 exclusive) and are clipped
// to the framebuffer before mapping to tile ranges.

class TileGrid {
public:
    TileGrid() = default;

    TileGrid(int width, int height, int tile_size)
        : m_width(width)
        , m_height(height)
        , m_tile_size(tile_size)
    {
        if (m_tile_size <= 0) m_tile_size = 1;
        m_cols = (m_width  + m_tile_size - 1) / m_tile_size;
        m_rows = (m_height + m_tile_size - 1) / m_tile_size;
    }

    [[nodiscard]] int width()     const { return m_width; }
    [[nodiscard]] int height()    const { return m_height; }
    [[nodiscard]] int tile_size() const { return m_tile_size; }
    [[nodiscard]] int cols()      const { return m_cols; }
    [[nodiscard]] int rows()      const { return m_rows; }
    [[nodiscard]] int tile_count() const { return m_cols * m_rows; }

    // ── Pixel-space bounding box for tile (tx, ty) ───────────────────────
    //     x1/y1 are exclusive.  Edge tiles may be smaller than tile_size.
    [[nodiscard]] BBox tile_bounds(int tx, int ty) const {
        BBox b;
        b.x0 = tx * m_tile_size;
        b.y0 = ty * m_tile_size;
        b.x1 = std::min(b.x0 + m_tile_size, m_width);
        b.y1 = std::min(b.y0 + m_tile_size, m_height);
        return b;
    }

    // ── Tile range for a pixel-space bbox (x1/y1 exclusive) ──────────────
    //     Returns an empty TileRect (0,0,0,0) when the bbox is empty or
    //     entirely outside the framebuffer.  x1/y1 are exclusive.
    [[nodiscard]] TileRect tiles_for_bbox(const BBox& bbox) const {
        BBox b = bbox;
        b.clip_to(m_width, m_height);

        if (b.is_empty()) return TileRect{0, 0, 0, 0};

        int tx0 = std::clamp(b.x0 / m_tile_size, 0, m_cols);
        int ty0 = std::clamp(b.y0 / m_tile_size, 0, m_rows);

        // b.x1 is exclusive; add (tile_size - 1) before division to
        // include the final tile only when the bbox actually covers it.
        int tx1 = std::clamp((b.x1 + m_tile_size - 1) / m_tile_size, 0, m_cols);
        int ty1 = std::clamp((b.y1 + m_tile_size - 1) / m_tile_size, 0, m_rows);

        return TileRect{tx0, ty0, tx1, ty1};
    }

    // ── Linear index helpers ─────────────────────────────────────────────
    [[nodiscard]] int index(int tx, int ty) const {
        return ty * m_cols + tx;
    }

    [[nodiscard]] TileCoord coord(int index) const {
        return TileCoord{index % m_cols, index / m_cols};
    }

private:
    int m_width{0};
    int m_height{0};
    int m_tile_size{0};
    int m_cols{0};
    int m_rows{0};
};

} // namespace chronon3d::raster
