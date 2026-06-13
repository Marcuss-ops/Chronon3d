#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// frame_parameter_block.hpp — B6: FrameParameterBlock
//
// A pre-allocated container for per-frame dynamic data that the refresh
// pipeline writes into before execution.  Eliminates per-frame allocation
// of parameter vectors by sizing once during warm-up and reusing the same
// storage across frames.
//
// Invariants enforced by tests:
//   1. No residual values — after refresh, stale data from a previous frame
//      is overwritten, never left behind.
//   2. Capacity stable — after warm-up, capacity() never grows, even after
//      1000 consecutive refresh iterations.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <cstdint>
#include <vector>

namespace chronon3d::graph {

// ── Per-layer transform block ────────────────────────────────────────────────
//
/// Compact structure holding all per-frame dynamic data for one layer binding.
/// Sized once at warm-up; refresh() writes into existing slots without
/// reallocation.
struct LayerParameterBlock {
    /// World transform (matrix + opacity) that the TransformNode reads.
    /// Set to identity (no-op) when the layer has no transform binding.
    Mat4   matrix{1.0f};
    f32    opacity{1.0f};

    /// Optional effect parameter overrides (e.g. animated blur radius).
    /// Only populated for layers that have active effect animations.
    f32    effect_blur_radius{0.0f};
    bool   has_animated_blur{false};

    /// Whether this block was written by the current frame's refresh.
    /// Reset to false at the start of each refresh; set to true when
    /// the corresponding binding is updated.  Any block still false
    /// after refresh signals a stale/unused entry.
    bool   refreshed_this_frame{false};
};

// ── FrameParameterBlock ──────────────────────────────────────────────────────
//
/// Holds one LayerParameterBlock per binding entry, pre-sized at warm-up.
/// The refresh pipeline writes to blocks[i] for each binding i without
/// growing the vector.
struct FrameParameterBlock {
    /// Indexed by SceneBinding position in the binding table.
    std::vector<LayerParameterBlock> layers;

    /// Total number of successful writes (for diagnostics / telemetry).
    uint64_t refresh_count{0};

    /// Initialize the block with `count` entries, preserving any existing
    /// entries if the count matches.  Returns true if no reallocation occurred.
    bool warm_up(size_t count) {
        if (count == layers.size()) {
            return true;  // already sized — no reallocation
        }
        // Clear all state (residual guards)
        layers.clear();
        layers.resize(count);
        refresh_count = 0;
        return false;  // reallocation happened
    }

    /// Reset per-frame state without changing capacity.
    void begin_frame() {
        refresh_count = 0;
        for (auto& l : layers) {
            l.refreshed_this_frame = false;
        }
    }

    /// Returns the stable capacity after warm-up.
    [[nodiscard]] size_t capacity() const noexcept {
        return layers.capacity();
    }

    /// Returns the current number of entries.
    [[nodiscard]] size_t size() const noexcept {
        return layers.size();
    }

    /// Returns true when the block has no entries (uninitialised).
    [[nodiscard]] bool empty() const noexcept {
        return layers.empty();
    }

    /// Access a layer block by index.  No bounds check — caller must ensure
    /// the index is < size().
    [[nodiscard]] LayerParameterBlock& operator[](size_t idx) {
        return layers[idx];
    }

    [[nodiscard]] const LayerParameterBlock& operator[](size_t idx) const {
        return layers[idx];
    }
};

} // namespace chronon3d::graph
