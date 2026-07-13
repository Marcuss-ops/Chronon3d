#pragma once

// ── for_each_tile — kernel-level parallel dispatch via tiling ─────────
//
// Pure free-function dispatch (lives in `chronon3d::scheduler` namespace).
// Per user-spec verbatim: provides a single `for_each_tile(bbox, tile_size,
// body)` API that kernel callers (blur / glow / resample) use to
// partition a `dirty_bbox` into tiles and execute the per-tile body
// either serially OR in parallel depending on tile cardinality.
//
// Per AGENTS.md §regole "NON imporre scaling uniformemente: scene
// seriali (B00 EmptyFrame) devono rimanere seriali", the function
// GATES automatically on:
//   1. Empty bbox OR tile_count == 0 → no-op (no body invocation).
//   2. tile_count == 1 → single serial invocation (avoids TBB
//      dispatcher overhead for trivial passes).
//   3. tile_count >= 2 → parallel via `parallel_for_tracked` (the
//      canonical TBB wrapper that records `tbb_active_workers_peak`
//      telemetry, matching the rest of the engine).
//
// Per-tile body invocation order is row-major (top→bottom,
// left→right within row); caller-controlled for determinism. ABI-
// stable, exception-safe, NO global mutable state, NO new singleton
// per AGENTS.md "no nuovi singleton/registry/resolver/cache senza ADR".

#include <cstddef>

#include <chronon3d/core/scheduler/tile_size.hpp>
#include <chronon3d/core/parallel_tracked.hpp>    // `parallel_for_tracked`

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace chronon3d {
namespace scheduler {

/// Partition `bbox` into tiles of `ts.width × ts.height` pixels and
/// invoke `body(Tile)` once per tile.
///
/// Routing rules (per §honesty + non-uniform-scaling invariant):
///   - `bbox.w == 0` or `bbox.h == 0` → no-op.
///   - `ts.width <= 0` or `ts.height <= 0` → no-op (degenerate).
///   - `compute_tile_count(bbox, ts) == 1` → single serial
///     invocation (B00 EmptyFrame + small dirty-rect passes).
///   - Otherwise → `tbb::parallel_for` over the tile index range,
///     wrapped by `parallel_for_tracked` so worker-pool telemetry
///     counts this dispatch like any other render-loop parallel_for.
///
/// Tile-coverage exactness invariant (per
/// `tests/kernel/test_kernel_tiling.cpp`): the union of all body
/// invocations' tiles covers `bbox` exactly — no overlap, no gaps.
/// Edge tiles at the right or bottom of bbox are smaller than `ts`
/// when `bbox.w` or `bbox.h` is not a multiple of the corresponding
/// dimension.
///
/// ABI: `body` is invoked with a `Tile` value (cheap to copy).
/// Threading: each parallel iteration executes `body(...)` once;
/// `body` MUST be thread-safe across invocations (no shared mutable
/// state outside the per-tile arguments captured by the caller).
template <typename Fn>
inline void for_each_tile(const raster::BBox& bbox, TileSize tile_size, Fn&& body) {
    const std::size_t n = compute_tile_count(bbox, tile_size);
    if (n == 0) return;          // empty/dirty-bbox
    if (n == 1) {               // serial: single tile, no dispatch
        body(compute_tile(bbox, tile_size, 0));
        return;
    }
    // Parallel: tile_count >= 2.
    parallel_for_tracked(
        tbb::blocked_range<std::size_t>(0, n),
        [&](const tbb::blocked_range<std::size_t>& range) {
            for (std::size_t i = range.begin(); i < range.end(); ++i) {
                body(compute_tile(bbox, tile_size, i));
            }
        });
}

} // namespace scheduler
} // namespace chronon3d
