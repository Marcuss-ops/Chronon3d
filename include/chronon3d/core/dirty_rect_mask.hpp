#pragma once

#include <chronon3d/math/raster_utils.hpp>
#include <cstdint>
#include <bitset>
#include <vector>

namespace chronon3d::raster {

class DirtyRectMask {
public:
    static constexpr int k_tile_size = 64;
    static constexpr int k_max_tiles = 1024; // supports up to 2048x2048 at 64x64 tiles

    DirtyRectMask() = default;

    DirtyRectMask(int width, int height) : m_width(width), m_height(height) {
        m_tiles_x = (width + k_tile_size - 1) / k_tile_size;
        m_tiles_y = (height + k_tile_size - 1) / k_tile_size;
    }

    void mark_dirty(const BBox& bbox) {
        if (m_tiles_x == 0 || m_tiles_y == 0) return;

        int tx0 = std::clamp(bbox.x0 / k_tile_size, 0, m_tiles_x - 1);
        int ty0 = std::clamp(bbox.y0 / k_tile_size, 0, m_tiles_y - 1);
        int tx1 = std::clamp(bbox.x1 / k_tile_size, 0, m_tiles_x - 1);
        int ty1 = std::clamp(bbox.y1 / k_tile_size, 0, m_tiles_y - 1);

        for (int y = ty0; y <= ty1; ++y) {
            for (int x = tx0; x <= tx1; ++x) {
                int index = y * m_tiles_x + x;
                if (index < k_max_tiles) {
                    m_mask.set(index);
                }
            }
        }
    }

    void mark_all_dirty() {
        m_mask.set();
    }

    void clear() {
        m_mask.reset();
    }

    [[nodiscard]] bool is_dirty(int x, int y) const {
        if (m_tiles_x == 0 || m_tiles_y == 0) return false;
        int tx = x / k_tile_size;
        int ty = y / k_tile_size;
        if (tx < 0 || tx >= m_tiles_x || ty < 0 || ty >= m_tiles_y) return false;
        int index = ty * m_tiles_x + tx;
        if (index < k_max_tiles) {
            return m_mask.test(index);
        }
        return false;
    }

    [[nodiscard]] bool is_tile_dirty(int tx, int ty) const {
        if (tx < 0 || tx >= m_tiles_x || ty < 0 || ty >= m_tiles_y) return false;
        int index = ty * m_tiles_x + tx;
        if (index < k_max_tiles) {
            return m_mask.test(index);
        }
        return false;
    }

    [[nodiscard]] bool is_empty() const {
        return m_mask.none();
    }

    [[nodiscard]] int tiles_x() const { return m_tiles_x; }
    [[nodiscard]] int tiles_y() const { return m_tiles_y; }

private:
    int m_width{0};
    int m_height{0};
    int m_tiles_x{0};
    int m_tiles_y{0};
    std::bitset<k_max_tiles> m_mask;
};

} // namespace chronon3d::raster
