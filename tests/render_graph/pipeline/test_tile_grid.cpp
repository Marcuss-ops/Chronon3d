#include <doctest/doctest.h>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>

using namespace chronon3d::raster;

// ─────────────────────────────────────────────────────────────────────────────
// TileGrid tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TileGrid") {

TEST_CASE("TileGrid dimensions") {
    TileGrid grid(100, 80, 32);

    CHECK(grid.width() == 100);
    CHECK(grid.height() == 80);
    CHECK(grid.tile_size() == 32);
    CHECK(grid.cols() == 4);   // ceil(100/32) = 4
    CHECK(grid.rows() == 3);   // ceil(80/32) = 3
    CHECK(grid.tile_count() == 12);
}

TEST_CASE("TileGrid tile_bounds for interior tile") {
    TileGrid grid(100, 80, 32);

    BBox b = grid.tile_bounds(0, 0);
    CHECK(b.x0 == 0);
    CHECK(b.y0 == 0);
    CHECK(b.x1 == 32);
    CHECK(b.y1 == 32);
}

TEST_CASE("TileGrid tile_bounds for last tile (smaller)") {
    TileGrid grid(100, 80, 32);

    // Last column tile
    BBox bx = grid.tile_bounds(3, 0);
    CHECK(bx.x0 == 96);
    CHECK(bx.y0 == 0);
    CHECK(bx.x1 == 100);  // only 4 pixels wide
    CHECK(bx.y1 == 32);

    // Last row tile: 32*2 = 64, so last row should be [64, 80)
    BBox by = grid.tile_bounds(0, 2);
    CHECK(by.y0 == 64);
    CHECK(by.y1 == 80);

    // Bottom-right corner tile
    BBox bc = grid.tile_bounds(3, 2);
    CHECK(bc.x0 == 96);
    CHECK(bc.y0 == 64);
    CHECK(bc.x1 == 100);
    CHECK(bc.y1 == 80);
}

TEST_CASE("TileGrid tiles_for_bbox: single tile") {
    TileGrid grid(128, 128, 32);

    // BBox entirely within tile (0,0)
    BBox box{10, 10, 20, 20};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 1);
    CHECK(tr.y1 == 1);
}

TEST_CASE("TileGrid tiles_for_bbox: crosses tile boundary") {
    TileGrid grid(128, 128, 32);

    // BBox that straddles tile boundaries: 31,31 → 33,33
    BBox box{31, 31, 33, 33};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 2);  // includes tiles 0 and 1
    CHECK(tr.y1 == 2);
}

TEST_CASE("TileGrid tiles_for_bbox: touches single pixel in tile") {
    TileGrid grid(128, 128, 32);

    // One pixel at (63,63) — should only mark tile (1,1)
    BBox box{63, 63, 64, 64};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 1);
    CHECK(tr.y0 == 1);
    CHECK(tr.x1 == 2);
    CHECK(tr.y1 == 2);
}

TEST_CASE("TileGrid tiles_for_bbox: empty bbox") {
    TileGrid grid(100, 80, 32);

    BBox box{0, 0, 0, 0};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 0);
    CHECK(tr.y1 == 0);
}

TEST_CASE("TileGrid tiles_for_bbox: outside frame") {
    TileGrid grid(100, 80, 32);

    // Entirely outside
    BBox box{200, 200, 300, 300};
    auto tr = grid.tiles_for_bbox(box);

    // Clipped to frame: 200,200 → 300,300 becomes 100,80 → 100,80 = empty
    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 0);
    CHECK(tr.y1 == 0);
}

TEST_CASE("TileGrid tiles_for_bbox: partially outside frame") {
    TileGrid grid(100, 80, 32);

    // Partially outside (negative origin)
    BBox box{-10, -10, 50, 50};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 2);  // ceil(50/32) = 2
    CHECK(tr.y1 == 2);
}

TEST_CASE("TileGrid tiles_for_bbox: exactly on tile edge") {
    TileGrid grid(128, 128, 32);

    // BBox ending exactly at tile boundary
    BBox box{0, 0, 64, 64};
    auto tr = grid.tiles_for_bbox(box);

    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 2);
    CHECK(tr.y1 == 2);
}

TEST_CASE("TileGrid index/coord roundtrip") {
    TileGrid grid(100, 80, 32);

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            int idx = grid.index(tx, ty);
            TileCoord c = grid.coord(idx);
            CHECK(c.x == tx);
            CHECK(c.y == ty);
        }
    }
}

TEST_CASE("TileGrid with tile_size = 1") {
    TileGrid grid(10, 10, 1);

    CHECK(grid.cols() == 10);
    CHECK(grid.rows() == 10);
    CHECK(grid.tile_count() == 100);

    BBox b = grid.tile_bounds(5, 5);
    CHECK(b.x0 == 5);
    CHECK(b.y0 == 5);
    CHECK(b.x1 == 6);
    CHECK(b.y1 == 6);
}

TEST_CASE("TileGrid with exact division") {
    TileGrid grid(128, 128, 64);

    CHECK(grid.cols() == 2);
    CHECK(grid.rows() == 2);
    CHECK(grid.tile_count() == 4);

    BBox b = grid.tile_bounds(1, 1);
    CHECK(b.x0 == 64);
    CHECK(b.y0 == 64);
    CHECK(b.x1 == 128);
    CHECK(b.y1 == 128);
}

TEST_CASE("TileGrid bounding box larger than grid") {
    TileGrid grid(64, 64, 32);

    BBox box{0, 0, 200, 200};
    auto tr = grid.tiles_for_bbox(box);

    // Should clip to frame: 0,0 → 64,64
    CHECK(tr.x0 == 0);
    CHECK(tr.y0 == 0);
    CHECK(tr.x1 == 2); // ceil(64/32) = 2
    CHECK(tr.y1 == 2);
}

} // TEST_SUITE("TileGrid")

// ─────────────────────────────────────────────────────────────────────────────
// DirtyTileMask tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("DirtyTileMask") {

TEST_CASE("DirtyTileMask: initial state is clean") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);

    CHECK_FALSE(mask.any());
    CHECK(mask.dirty_count() == 0);

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            CHECK_FALSE(mask.is_dirty(tx, ty));
        }
    }
}

TEST_CASE("DirtyTileMask: mark_bbox single tile") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);

    mask.mark_bbox(grid, BBox{10, 10, 20, 20});

    CHECK(mask.any());
    CHECK(mask.dirty_count() == 1);
    CHECK(mask.is_dirty(0, 0));
    CHECK_FALSE(mask.is_dirty(1, 0));
    CHECK_FALSE(mask.is_dirty(0, 1));
}

TEST_CASE("DirtyTileMask: mark_bbox crossing boundaries") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);

    mask.mark_bbox(grid, BBox{31, 31, 33, 33});

    CHECK(mask.is_dirty(0, 0));
    CHECK(mask.is_dirty(1, 0));
    CHECK(mask.is_dirty(0, 1));
    CHECK(mask.is_dirty(1, 1));
    CHECK(mask.dirty_count() == 4);
}

TEST_CASE("DirtyTileMask: mark_all") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_all();

    CHECK(mask.any());
    CHECK(mask.dirty_count() == 12);

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            CHECK(mask.is_dirty(tx, ty));
        }
    }
}

TEST_CASE("DirtyTileMask: clear after mark") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_all();
    mask.clear();

    CHECK_FALSE(mask.any());
    CHECK(mask.dirty_count() == 0);
}

TEST_CASE("DirtyTileMask: mark_tile individual") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_tile(2, 1);

    CHECK(mask.dirty_count() == 1);
    CHECK(mask.is_dirty(2, 1));
    CHECK_FALSE(mask.is_dirty(0, 0));
    CHECK_FALSE(mask.is_dirty(3, 2));
}

TEST_CASE("DirtyTileMask: multiple marks are idempotent") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_tile(1, 0);
    mask.mark_tile(1, 0);
    mask.mark_tile(1, 0);

    CHECK(mask.dirty_count() == 1);
}

TEST_CASE("DirtyTileMask: for_each_dirty_tile iteration") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_tile(0, 0);
    mask.mark_tile(3, 0);
    mask.mark_tile(0, 2);
    mask.mark_tile(3, 2);

    int count = 0;
    bool found00 = false, found30 = false, found02 = false, found32 = false;

    mask.for_each_dirty_tile(grid, [&](int tx, int ty) {
        ++count;
        if (tx == 0 && ty == 0) found00 = true;
        if (tx == 3 && ty == 0) found30 = true;
        if (tx == 0 && ty == 2) found02 = true;
        if (tx == 3 && ty == 2) found32 = true;
    });

    CHECK(count == 4);
    CHECK(found00);
    CHECK(found30);
    CHECK(found02);
    CHECK(found32);
}

TEST_CASE("DirtyTileMask: for_each_dirty_tile on empty mask") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    int count = 0;
    mask.for_each_dirty_tile(grid, [&](int, int) { ++count; });

    CHECK(count == 0);
}

TEST_CASE("DirtyTileMask: dirty_count matches iteration count") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);

    // Mark checkerboard pattern
    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = ty % 2; tx < grid.cols(); tx += 2) {
            mask.mark_tile(tx, ty);
        }
    }

    const int dc = mask.dirty_count();
    int iter_count = 0;
    mask.for_each_dirty_tile(grid, [&](int, int) { ++iter_count; });

    CHECK(dc == iter_count);
    CHECK(dc == 8); // 4x4 grid, checkerboard = 8
}

TEST_CASE("DirtyTileMask: large mask (many tiles)") {
    // 1920x1080 at tile_size=128 → 15 × 9 = 135 tiles
    TileGrid grid(1920, 1080, 128);
    DirtyTileMask mask(grid);

    // Should handle large tile count correctly
    CHECK(grid.cols() == 15);
    CHECK(grid.rows() == 9);
    CHECK(grid.tile_count() == 135);

    mask.mark_bbox(grid, BBox{0, 0, 1920, 1080});
    CHECK(mask.dirty_count() == 135);
    CHECK(mask.any());

    // Mark a sub-region
    mask.clear();
    mask.mark_bbox(grid, BBox{500, 300, 600, 400});
    CHECK(mask.dirty_count() > 0);
    CHECK(mask.dirty_count() <= 4); // small region, few tiles
}

TEST_CASE("DirtyTileMask: mark_bbox empty bbox") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);

    mask.mark_bbox(grid, BBox{0, 0, 0, 0});
    CHECK(mask.dirty_count() == 0);
    CHECK_FALSE(mask.any());
}

TEST_CASE("DirtyTileMask: mark_bbox outside frame") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_bbox(grid, BBox{200, 200, 300, 300});
    CHECK(mask.dirty_count() == 0);
}

TEST_CASE("DirtyTileMask: bits vector consistency") {
    TileGrid grid(100, 80, 32);
    DirtyTileMask mask(grid);

    mask.mark_tile(0, 0);
    mask.mark_tile(3, 2);

    const auto& bits = mask.bits();
    CHECK(bits.size() == 1); // Only 12 tiles → fits in 1 uint64_t
    CHECK(bits[0] != 0);
}

} // TEST_SUITE("DirtyTileMask")
