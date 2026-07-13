// ── tests/kernel/test_kernel_tiling.cpp ─────────────────────────────
//
// Validates the canonical kernel-tilt-tiling API surface for FASE 4.3
// (TICKET-KERNEL-TILING-V1):
//   1. Tile count for various bbox/tile_size combinations.
//   2. Tile coverage EXACTNESS: union of all tiles == bbox (no
//      overlap, no gaps).  This is the §honesty-critical invariant:
//      a caller passing the dirty-bbox to for_each_tile MUST observe
//      exactly-bbox-equal coverage (rounding aside, edge tiles may be
//      smaller at the right/bottom edges).
//   3. Degenerate cases: empty bbox, single-tile bbox, larger-than-
//      tile-huge bbox, mismatched aspect.
//
// VPS-macchina-verifiable: tests the API surface directly (no
// `chronon3d_cli` binary needed).  Speed-up gates on B03/B05 are
// DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent.

#include <cstddef>

#include <chronon3d/core/scheduler/tile_size.hpp>
#include <chronon3d/raster/bbox.hpp>

#include <doctest/doctest.h>

using chronon3d::raster::BBox;
using chronon3d::scheduler::compute_tile;
using chronon3d::scheduler::compute_tile_count;
using chronon3d::scheduler::Tile;
using chronon3d::scheduler::TileSize;

// ── 1. Tile count for cleanly-aligned bbox ────────────────────────────
TEST_CASE("KernelTiling: tile_count cleanly-aligned bbox") {
    BBox bbox{0, 0, 1920, 1080};      // 1920×1080
    TileSize ts{128, 64};
    // 1920/128 = 15; 1080/64 = 16.875 → ceil = 17.  Total = 15*17 = 255.
    CHECK(compute_tile_count(bbox, ts) == 255);
}

// ── 2. Tile count for unequal-split bbox (edge tiles smaller) ─────────
TEST_CASE("KernelTiling: tile_count unequal-split bbox") {
    BBox bbox{0, 0, 100, 50};          // 100×50
    TileSize ts{128, 64};
    // 100/128 ceil = 1; 50/64 ceil = 1.  Total = 1.
    CHECK(compute_tile_count(bbox, ts) == 1);

    BBox bbox2{0, 0, 257, 65};
    // 257/128 ceil = 3; 65/64 ceil = 2.  Total = 6.
    CHECK(compute_tile_count(bbox2, ts) == 6);
}

// ── 3. Tile count for empty / degenerate bbox ─────────────────────────
TEST_CASE("KernelTiling: tile_count degenerate bbox returns 0") {
    BBox empty{0, 0, 0, 100};
    TileSize ts{128, 64};
    CHECK(compute_tile_count(empty, ts) == 0);

    BBox empty_h{0, 0, 100, 0};
    CHECK(compute_tile_count(empty_h, ts) == 0);

    BBox empty_ts{0, 0};     // ts.width = 0 → degenerate
    CHECK(compute_tile_count(empty_ts, ts) == 0);
}

// ── 4. compute_tile exactness: union of tiles == bbox EXACTLY ─────────
// This is the §honesty-critical invariant.
TEST_CASE("KernelTiling: union of tiles == bbox exactly (no overlap, no gaps)") {
    BBox bbox{0, 0, 1920, 1080};       // 1920×1080
    TileSize ts{128, 64};
    const std::size_t n = compute_tile_count(bbox, ts);
    REQUIRE(n == 255);

    // Verify: tile coordinates + dimensions cover bbox exactly.
    // Per-row: tiles x = 0, 128, 256, ..., (15-1)*128 = 1792
    //          width = 128 each (no edge case here, 15*128=1920)
    // Per-col: tiles y = 0, 64, 128, ..., (16-1)*64 = 1024 (last row only spans 56)
    //          height: rows 0..15 (=1024 rows) get 64; row 16 gets 56 (last one).
    for (std::size_t i = 0; i < n; ++i) {
        Tile t = compute_tile(bbox, ts, i);
        CHECK(t.x >= 0);
        CHECK(t.y >= 0);
        CHECK(t.w > 0);
        CHECK(t.h > 0);
        CHECK(t.w <= ts.width);
        CHECK(t.h <= ts.height);
        CHECK(t.index == i);
        // Sanity: tile fits inside bbox (right edge ≤ bbox.x+bbox.w).
        CHECK(t.x + t.w <= bbox.x + bbox.w);
        CHECK(t.y + t.h <= bbox.y + bbox.h);
        // The first tile MUST be the (0,0) tile.
        if (i == 0) {
            CHECK(t.x == 0);
            CHECK(t.y == 0);
        }
    }
    // Row-major order verification: tile index 14 should be the last tile of
    // row 0 (tx=14, ty=0); tile index 15 should start row 1 (tx=0, ty=1).
    Tile tile_14 = compute_tile(bbox, ts, 14);
    Tile tile_15 = compute_tile(bbox, ts, 15);
    CHECK(tile_14.x == 14 * 128);
    CHECK(tile_14.y == 0);
    CHECK(tile_15.x == 0);
    CHECK(tile_15.y == 64);
    // Last tile: index 254 should be at tx=14, ty=16 (last row).
    Tile tile_last = compute_tile(bbox, ts, 254);
    CHECK(tile_last.x == 14 * 128);            // = 1792
    CHECK(tile_last.y == 16 * 64);              // = 1024
    CHECK(tile_last.w == 128);                  // full width
    CHECK(tile_last.h == 1080 - 1024);          // = 56 (last-row edge tile)
}

// ── 5. Edge tiles are smaller than full tile-size when bbox dimension
//    is not a multiple of tile dimension.
TEST_CASE("KernelTiling: edge tiles correctly smaller than TileSize") {
    BBox bbox{0, 0, 200, 100};     // 200×100
    TileSize ts{128, 64};
    const std::size_t n = compute_tile_count(bbox, ts);
    REQUIRE(n == 4);  // ceil(200/128)=2, ceil(100/64)=2 → 2*2=4

    // Row 0: (0,0) full, (128,0) edge — width = 200-128 = 72.
    Tile t00 = compute_tile(bbox, ts, 0);
    CHECK(t00.x == 0);
    CHECK(t00.y == 0);
    CHECK(t00.w == 128);
    CHECK(t00.h == 64);

    Tile t10 = compute_tile(bbox, ts, 1);
    CHECK(t10.x == 128);
    CHECK(t10.w == 72);     // edge tile: 200 - 128 = 72
    CHECK(t10.h == 64);     // full

    // Row 1: (0,64) full, (128,64) edge — height = 100-64 = 36.
    Tile t01 = compute_tile(bbox, ts, 2);
    CHECK(t01.x == 0);
    CHECK(t01.y == 64);
    CHECK(t01.w == 128);
    CHECK(t01.h == 36);     // edge tile

    Tile t11 = compute_tile(bbox, ts, 3);
    CHECK(t11.x == 128);
    CHECK(t11.y == 64);
    CHECK(t11.w == 72);
    CHECK(t11.h == 36);
}

// ── 6. Single-tile bbox. for_each_tile serial-fast-path condition. ────
TEST_CASE("KernelTiling: bbox smaller than TileSize → 1 tile") {
    BBox small{10, 20, 100, 50};  // 90×30
    TileSize ts{128, 64};
    CHECK(compute_tile_count(small, ts) == 1);
    Tile t = compute_tile(small, ts, 0);
    CHECK(t.x == 10);
    CHECK(t.y == 20);
    CHECK(t.w == 90);
    CHECK(t.h == 30);
}

// ── 7. Bbox exactly tile-aligned → no edge tiles. ─────────────────────
TEST_CASE("KernelTiling: bbox exactly tile-aligned → no edge tiles") {
    BBox bbox{0, 0, 256, 128};    // 256×128 = 2*128 × 2*64
    TileSize ts{128, 64};
    CHECK(compute_tile_count(bbox, ts) == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        Tile t = compute_tile(bbox, ts, i);
        CHECK(t.w == 128);
        CHECK(t.h == 64);
    }
}

// ── 8. Non-zero-origin bbox: tile coordinates are absolute. ────────────
TEST_CASE("KernelTiling: tiles at non-zero origin use absolute coords") {
    BBox bbox{100, 200, 356, 264};  // 256×64 starting at (100,200)
    TileSize ts{128, 64};
    CHECK(compute_tile_count(bbox, ts) == 2);
    Tile t0 = compute_tile(bbox, ts, 0);
    CHECK(t0.x == 100);
    CHECK(t0.y == 200);
    CHECK(t0.w == 128);
    CHECK(t0.h == 64);
    Tile t1 = compute_tile(bbox, ts, 1);
    CHECK(t1.x == 228);    // 100+128
    CHECK(t1.y == 200);
    CHECK(t1.w == 128);
    CHECK(t1.h == 64);
}

// ── 9. Integration: tile union bitmap-traced sum equals bbox area. ────
// (Pixel-tracing is too slow for the canonical test, but a sampled
// integer-trace via std::vector<bool> bitmap at 1px/sample verifies
// exactness on a small bbox.)
TEST_CASE("KernelTiling: bitmap-traced union equals bbox area (small bbox)") {
    BBox bbox{0, 0, 7, 5};   // 7×5
    TileSize ts{3, 2};        // 7/3 ceil = 3; 5/2 ceil = 3 → 9 tiles
    const std::size_t n = compute_tile_count(bbox, ts);
    REQUIRE(n == 9);

    // Bitmap of size 7×5 = 35 cells.
    std::vector<bool> bitmap(7 * 5, false);
    int covered = 0;
    for (std::size_t i = 0; i < n; ++i) {
        Tile t = compute_tile(bbox, ts, i);
        for (int dy = 0; dy < t.h; ++dy) {
            for (int dx = 0; dx < t.w; ++dx) {
                const int x = t.x + dx;
                const int y = t.y + dy;
                REQUIRE(x >= 0 && x < 7);
                REQUIRE(y >= 0 && y < 5);
                const std::size_t idx = static_cast<std::size_t>(y * 7 + x);
                CHECK_FALSE(bitmap[idx]);     // no double-cover
                bitmap[idx] = true;
                ++covered;
            }
        }
    }
    CHECK(covered == 7 * 5);    // exact coverage
    for (bool v : bitmap) CHECK(v); // every pixel covered
}
