#pragma once

// ── tile_size — Tile + TileSize value types + bbox-tiling math ─────────
//
// Pure data-layer types used by `for_each_tile` kernel-dispatch
// (`include/chronon3d/core/scheduler/for_each_tile.hpp`). Per AGENTS.md
// §regole "Cat-3 minimal-surface", these structs are introduced ONLY
// because the user-spec verbatim shape `TileSize{128, 64}` and `Tile t`
// requires new ABI symbols (existing `TileRect`/`TileCoord` in
// `include/chronon3d/core/tile_grid.hpp` cover grid-internal semantics
// and are NOT part of the kernel-dispatch surface).
//
// Invariants (test+review enforced by `tests/kernel/test_kernel_tiling.cpp`):
//   - Every tile returned by `compute_tile(bbox, ts, i)` has `tile.w ∈
//     (0, ts.width]` and `tile.h ∈ (0, ts.height]` (last-row tiles may
//     be smaller at the right/bottom edge).
//   - The union of all tiles across `i ∈ [0, compute_tile_count(...))`
//     covers the bbox EXACTLY: no overlap, no gaps.
//   - Iteration order is row-major (top→bottom, left→right within row)
//     so caller can index by `(tile_y, tile_x)` = `(i / nx, i % nx)`.

#include <cstddef>

#include <chronon3d/raster/bbox.hpp>  // raster::BBox

namespace chronon3d {
namespace scheduler {

/// Per-tile dimension (width × height in pixels). The 1st field names
/// follow user-spec verbatim `TileSize{128, 64}` brace-init order.
struct TileSize {
    int width{0};
    int height{0};

    constexpr TileSize() noexcept = default;
    constexpr TileSize(int w, int h) noexcept : width(w), height(h) {}
};

/// Single-tile geometry + iteration index. Caller receives this value
/// per `for_each_tile` invocation.
struct Tile {
    int x{0};                 ///< top-left x in pixel coords
    int y{0};                 ///< top-left y in pixel coords
    int w{0};                 ///< tile width ≤ TileSize.width
    int h{0};                 ///< tile height ≤ TileSize.height
    std::size_t index{0};     ///< 0…`compute_tile_count(bbox, ts)−1`
};

/// Tile count for `bbox` partitioned by `ts`. Returns 0 if either
/// dimension is empty / non-positive. Otherwise returns
/// `ceil(bbox.w / ts.width) × ceil(bbox.h / ts.height)`.
[[nodiscard]] constexpr std::size_t
compute_tile_count(const raster::BBox& bbox, TileSize ts) noexcept {
    const int bbox_w = bbox.x1 - bbox.x0;
    const int bbox_h = bbox.y1 - bbox.y0;
    if (ts.width <= 0 || ts.height <= 0) return std::size_t{0};
    if (bbox_w <= 0 || bbox_h <= 0)      return std::size_t{0};
    const std::size_t nx = static_cast<std::size_t>(
        (bbox_w + ts.width  - 1) / ts.width);
    const std::size_t ny = static_cast<std::size_t>(
        (bbox_h + ts.height - 1) / ts.height);
    return nx * ny;
}

/// Compute the i-th tile for `bbox` partitioned by `ts`. Iteration
/// order is row-major: `i = ty * nx + tx` where `nx = ceil(bbox.w /
/// ts.width)`. Edge tiles at the right or bottom of bbox may be
/// smaller than `TileSize.width` / `TileSize.height` (the "tail" tile
/// of an uneven split).
[[nodiscard]] constexpr Tile
compute_tile(const raster::BBox& bbox, TileSize ts, std::size_t i) noexcept {
    const int bbox_w = bbox.x1 - bbox.x0;
    const int bbox_h = bbox.y1 - bbox.y0;
    const std::size_t nx = static_cast<std::size_t>(
        (bbox_w + ts.width  - 1) / ts.width);
    const std::size_t ny = static_cast<std::size_t>(
        (bbox_h + ts.height - 1) / ts.height);
    const std::size_t tx = (nx > 0) ? (i % nx) : std::size_t{0};
    const std::size_t ty = (nx > 0) ? (i / nx) : std::size_t{0};
    const int x = bbox.x0 + static_cast<int>(tx) * ts.width;
    const int y = bbox.y0 + static_cast<int>(ty) * ts.height;
    const int xmax = bbox.x0 + bbox_w;
    const int ymax = bbox.y0 + bbox_h;
    const int w = (x + ts.width <= xmax) ? ts.width  : (xmax - x);
    const int h = (y + ts.height <= ymax) ? ts.height : (ymax - y);
    return Tile{x, y, w, h, i};
}

} // namespace scheduler
} // namespace chronon3d
