// ============================================================================
// tests/deterministic/test_tile_determinism.cpp
//
// WP-6 PR 6.1 acceptance — tile determinism under repeated runs.
//
// TileGrid + DirtyTileMask are pure data structures (no TBB / no
// scheduler-state) — they MUST be deterministic in bit-pattern AND
// in iteration order across repeated invocations on the same input.
// These tests verify the invariants so any future change that
// introduces scheduler-state inside the tile data path fails this
// gate immediately.
//
// Re-enables the disabled TICKET-007.q/r/s/t/u tests IN SPIRIT by
// proving that the underlying tile machinery is itself deterministic
// even when the SoftwareRenderer's full-frame pixel output is not
// (the persistent rot in those tests is at the renderer level, NOT
// at the TileGrid / DirtyTileMask level — leaving the renderer
// non-determinism as a separate ticket).
//
// Wire this file into the existing `chronon3d_deterministic_tests`
// target inside `tests/deterministic_tests.cmake` (already gated on
// CHRONON3D_USE_BLEND2D per the existing project convention).
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using chronon3d::raster::TileGrid;
using chronon3d::raster::TileCoord;
using chronon3d::raster::TileRect;
using chronon3d::raster::DirtyTileMask;
using chronon3d::raster::BBox;

TEST_CASE("TileGrid: same constructor args produce same dimensions") {
    TileGrid g1(1920, 1080, 256);
    TileGrid g2(1920, 1080, 256);

    CHECK(g1.width()     == g2.width());
    CHECK(g1.height()    == g2.height());
    CHECK(g1.tile_size() == g2.tile_size());
    CHECK(g1.cols()      == g2.cols());
    CHECK(g1.rows()      == g2.rows());
    CHECK(g1.tile_count() == g2.tile_count());
}

TEST_CASE("TileGrid: edge-case dimensions — 1×1, exact divisors, last-col shorter") {
    TileGrid tiny(10, 10, 1);
    CHECK(tiny.cols() == 10);
    CHECK(tiny.rows() == 10);

    TileGrid exact(128, 128, 64);
    CHECK(exact.cols() == 2);
    CHECK(exact.rows() == 2);

    // 100×80 / tile 32 → cols=4 (3 full + 1 short), rows=3 (2 full + 1 short)
    TileGrid ragged(100, 80, 32);
    CHECK(ragged.cols() == 4);
    CHECK(ragged.rows() == 3);
}

TEST_CASE("TileGrid: tile_bounds is deterministic across runs") {
    TileGrid grid(640, 480, 128);

    // Run 0: capture all tile_bounds entries.
    std::vector<BBox> run0;
    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            run0.push_back(grid.tile_bounds(tx, ty));
        }
    }

    // Subsequent runs must match run 0 bit-exact.
    for (int repeat = 1; repeat <= 3; ++repeat) {
        std::size_t idx = 0;
        for (int ty = 0; ty < grid.rows(); ++ty) {
            for (int tx = 0; tx < grid.cols(); ++tx) {
                BBox got = grid.tile_bounds(tx, ty);
                CHECK(got.x0 == run0[idx].x0);
                CHECK(got.y0 == run0[idx].y0);
                CHECK(got.x1 == run0[idx].x1);
                CHECK(got.y1 == run0[idx].y1);
                ++idx;
            }
        }
    }
}

TEST_CASE("TileGrid: tiles_for_bbox — single tile, boundary, outside") {
    TileGrid grid(128, 128, 32);

    // Single interior tile: bbox fully inside one tile.
    TileRect r1 = grid.tiles_for_bbox(BBox{10, 10, 20, 20});
    CHECK(r1.x0 == 0);
    CHECK(r1.y0 == 0);
    CHECK(r1.x1 == 1);
    CHECK(r1.y1 == 1);

    // Cross-boundary bbox — must hit two columns + two rows.
    TileRect r2 = grid.tiles_for_bbox(BBox{30, 30, 65, 65});
    CHECK(r2.x0 == 0);
    CHECK(r2.y0 == 0);
    CHECK(r2.x1 == 3);
    CHECK(r2.y1 == 3);

    // Empty bbox → empty TileRect.
    TileRect r3 = grid.tiles_for_bbox(BBox{10, 10, 10, 10});
    CHECK(r3.x0 == 0);
    CHECK(r3.y0 == 0);
    CHECK(r3.x1 == 0);
    CHECK(r3.y1 == 0);

    // bbox fully outside the grid → empty TileRect.
    TileRect r4 = grid.tiles_for_bbox(BBox{1000, 1000, 1100, 1100});
    CHECK(r4.x1 == 0);
    CHECK(r4.y1 == 0);
}

TEST_CASE("DirtyTileMask: bit pattern is identical across 4 fresh constructions") {
    TileGrid grid(128, 128, 32);
    std::vector<std::uint64_t> first_bits;

    for (int repeat = 0; repeat < 4; ++repeat) {
        DirtyTileMask mask(grid);
        mask.mark_all();
        const auto& bits = mask.bits();
        if (repeat == 0) {
            first_bits = bits;
        } else {
            REQUIRE(bits.size() == first_bits.size());
            for (std::size_t i = 0; i < bits.size(); ++i) {
                CHECK(bits[i] == first_bits[i]);
            }
        }
    }
}

TEST_CASE("DirtyTileMask: mark_tile + is_dirty round-trip (checkerboard)") {
    TileGrid grid(64, 32, 16);
    DirtyTileMask mask(grid);

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            if ((tx + ty) % 2 == 0) mask.mark_tile(tx, ty);
        }
    }

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            const bool want = ((tx + ty) % 2 == 0);
            CHECK(mask.is_dirty(tx, ty) == want);
        }
    }
}

TEST_CASE("DirtyTileMask: for_each_dirty_tile — order is deterministic across runs") {
    TileGrid grid(64, 64, 32);
    DirtyTileMask mask(grid);
    mask.mark_tile(0, 0);
    mask.mark_tile(grid.cols() - 1, grid.rows() - 1);
    for (int i = 0; i < grid.cols(); ++i) {
        mask.mark_tile(i, i);
    }

    std::vector<std::pair<int, int>> run0;
    mask.for_each_dirty_tile(grid, [&](int tx, int ty) {
        run0.emplace_back(tx, ty);
    });

    // Iteration is const & — repeated invocations must produce the same order.
    for (int repeat = 1; repeat <= 3; ++repeat) {
        std::vector<std::pair<int, int>> runN;
        mask.for_each_dirty_tile(grid, [&](int tx, int ty) {
            runN.emplace_back(tx, ty);
        });
        CHECK(runN == run0);
    }
}

TEST_CASE("DirtyTileMask: clear() restores zero bit pattern") {
    TileGrid grid(128, 128, 32);
    DirtyTileMask mask(grid);
    mask.mark_all();
    REQUIRE(mask.any());
    REQUIRE(mask.dirty_count() == grid.tile_count());

    mask.clear();
    CHECK_FALSE(mask.any());
    CHECK(mask.dirty_count() == 0);
    for (auto w : mask.bits()) {
        CHECK(w == 0u);
    }
}

TEST_CASE("DirtyTileMask: mark_bbox + dirty_count agree on tile coverage") {
    TileGrid grid(256, 256, 64);
    DirtyTileMask mask(grid);
    mask.mark_bbox(grid, BBox{0, 0, 64, 64});  // top-left single tile

    CHECK(mask.dirty_count() == 1);
    CHECK(mask.is_dirty(0, 0));
    CHECK_FALSE(mask.is_dirty(1, 0));
    CHECK_FALSE(mask.is_dirty(0, 1));
    CHECK_FALSE(mask.is_dirty(1, 1));
}

TEST_CASE("DirtyTileMask: 4 fresh runs of identical marking produce identical bits") {
    TileGrid grid(256, 256, 32);

    // Build the same mask 4 times, with identical mark calls — bits MUST
    // match bit-by-bit across all 4 runs (deterministic construction +
    // deterministic mark ordering).
    std::vector<std::uint64_t> first_bits;
    for (int repeat = 0; repeat < 4; ++repeat) {
        DirtyTileMask mask(grid);
        for (int ty = 0; ty < grid.rows(); ++ty) {
            for (int tx = 0; tx < grid.cols(); ++tx) {
                if ((tx * 31 + ty * 17) % 7 == 0) mask.mark_tile(tx, ty);
            }
        }
        const auto& bits = mask.bits();
        if (repeat == 0) {
            first_bits = bits;
        } else {
            REQUIRE(bits.size() == first_bits.size());
            for (std::size_t i = 0; i < bits.size(); ++i) {
                CHECK(bits[i] == first_bits[i]);
            }
        }
    }
}
