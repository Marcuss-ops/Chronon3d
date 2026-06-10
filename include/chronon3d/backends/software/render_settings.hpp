#pragma once

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <string>
#include <optional>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {

// ───────────────────────────────────────────────────────────────────────────
// DirtyRenderSettings
// ───────────────────────────────────────────────────────────────────────────
//
// All flags related to dirty-rect / dirty-bitmask / tile-based partial
// rendering live here.  This is the single source of truth for "is the
// dirty-rect optimization active and how aggressive should it be?".
//
// Replaces the previous flat fields in RenderSettings:
//   enable_dirty_rects, enable_dirty_bitmask, dirty_rects (V1),
//   enable_parallel_tiles, tile_size, tile_dirty_ratio_threshold.
//
// The two confusing flags from the previous design:
//
//   enable_dirty_rects  →  dirty.enabled  (V2 master switch)
//   dirty_rects         →  dirty.dirty_rects_v1  (V1 legacy force-enable)
//
// are now clearly separated; use is_active() to check if any version is on.
struct DirtyRenderSettings {
    /// V2 master switch.  When true, the pipeline computes per-layer bounding
    /// boxes and tries to re-render only the changed region of the screen.
    /// Equivalent to the previous `enable_dirty_rects`.
    bool enabled{true};

    /// V2 64×64 bitmask tracking.  When true, dirty pixels are recorded on a
    /// 64×64 grid in addition to the per-layer bounding boxes.  Required
    /// for tile-based execution to work.
    /// Equivalent to the previous `enable_dirty_bitmask`.
    bool use_bitmask{true};

    /// V1 legacy flag.  When true, graph nodes may restrict their work to
    /// the node's predicted bbox instead of the layer's true bbox.  This
    /// is a pre-V2 fallback and is rarely useful in modern scenes.
    /// Equivalent to the previous `dirty_rects` field.
    bool dirty_rects_v1{false};

    /// Attempt tile-based execution when tile_size > 0 and the bitmask is
    /// enabled.  Tile-based execution re-runs the graph per dirty tile and
    /// is incompatible with spatial effects (blur/glow/bloom/shadow/etc.)
    /// — see TileExecutionPolicy.
    bool use_tiles{true};

    /// Process dirty tiles in parallel via tbb::parallel_for.
    /// When false, tiles are processed sequentially in a single-threaded loop.
    /// Equivalent to the previous `enable_parallel_tiles`.
    bool parallel_tiles{true};

    /// Tile size in pixels (e.g. 64, 128, 256).  0 = disabled.
    /// Equivalent to the previous `tile_size`.
    int tile_size{0};

    /// Maximum dirty ratio (0.0–1.0) that still enables tile execution.
    /// When the fraction of dirty screen pixels exceeds this threshold,
    /// tile execution is skipped in favour of a single full-frame pass.
    /// Example: 0.30 → skip tile execution when >30% of the screen is dirty.
    /// Equivalent to the previous `tile_dirty_ratio_threshold`.
    double tile_dirty_ratio_threshold{0.30};

    /// True if any version of the dirty-rect optimization is enabled.
    /// Preserves the OR semantics of the previous
    /// `enable_dirty_rects || dirty_rects || enable_dirty_bitmask`.
    [[nodiscard]] constexpr bool is_active() const noexcept {
        return enabled || dirty_rects_v1 || use_bitmask;
    }

    /// True if tile-based execution should be attempted given the current
    /// settings.  Equivalent to `tile_size > 0 && use_bitmask && use_tiles`.
    [[nodiscard]] constexpr bool tiles_active() const noexcept {
        return use_tiles && tile_size > 0 && use_bitmask;
    }
};

// ───────────────────────────────────────────────────────────────────────────
// CompositingSettings
// ───────────────────────────────────────────────────────────────────────────
//
// Flags that affect how framebuffers are composed together (separate from
// dirty-rect / tile logic, which lives in DirtyRenderSettings).
struct CompositingSettings {
    /// When true, the compositor attempts in-place composition to avoid
    /// costly framebuffer copies.
    /// Equivalent to the previous `optimize_compositing`.
    bool optimize_compositing{true};
};

// ───────────────────────────────────────────────────────────────────────────
// DiagnosticSettings
// ───────────────────────────────────────────────────────────────────────────
//
// Flags for debug overlays and per-frame inspection output.
struct DiagnosticSettings {
    /// Enable diagnostic overlays and info in the output.
    /// Equivalent to the previous `diagnostic`.
    bool enabled{false};

    /// Log a graph preflight report before rendering each frame.
    /// Equivalent to the previous `diagnostic_plan`.
    bool plan{false};

    /// Optional path pattern for the graph preflight report output.
    /// If no #### placeholder is present, a _0000 frame suffix is inserted.
    /// Equivalent to the previous `diagnostic_plan_output`.
    std::string plan_output;

    /// Path to the font used for diagnostic overlays.
    /// Equivalent to the previous `diagnostic_font_path`.
    std::string font_path;
};

// ───────────────────────────────────────────────────────────────────────────
// RenderSettings
// ───────────────────────────────────────────────────────────────────────────
//
// Top-level rendering configuration.  Grouped by responsibility so the
// related flags live next to each other.  In particular, all dirty-rect /
// tile / bitmask knobs are under `dirty`; see DirtyRenderSettings.
struct RenderSettings {
    /**
     * Compatibility toggle for the modular graph coordinate convention.
     * false = top-left scene coordinates, true = centered modular graph coordinates.
     */
    bool use_modular_graph{true};

    /**
     * Global motion blur settings.
     */
    MotionBlurSettings motion_blur{};

    /**
     * Anti-aliasing factor (Super Sampling).
     * 1.0 = none, 2.0 = 2x2 grid (4 samples/pixel).
     */
    float ssaa_factor{1.0f};

    /// All dirty-rect, bitmask, and tile-based partial rendering flags.
    DirtyRenderSettings dirty{};

    /// Compositing behavior (in-place composition, etc.).
    CompositingSettings compositing{};

    /// Diagnostic overlay and preflight report settings.
    DiagnosticSettings diagnostics{};

    /**
     * Diagnostic: when true, composite_normal_premul uses the safe scalar fallback
     * instead of the Highway SIMD path. Helps isolate SIMD-related rendering bugs.
     */
    bool force_scalar_normal_blend{false};
};

} // namespace chronon3d
