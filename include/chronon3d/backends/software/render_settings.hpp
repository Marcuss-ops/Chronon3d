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
//   enable_dirty_bitmask →  dirty.use_bitmask
//
// are now clearly separated; use is_active() to check if any version is on.
struct DirtyRenderSettings {
    /// V2 master switch.  When true, the pipeline computes per-layer bounding
    /// boxes and tries to re-render only the changed region of the screen.
    /// Equivalent to the previous `enable_dirty_rects` and `dirty_rects`.
    bool enabled{true};

    /// V2 64×64 bitmask tracking.  When true, dirty pixels are recorded on a
    /// 64×64 grid in addition to the per-layer bounding boxes.  Required
    /// for tile-based execution to work.
    /// Equivalent to the previous `enable_dirty_bitmask`.
    bool use_bitmask{true};

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
    [[nodiscard]] constexpr bool is_active() const noexcept {
        return enabled || use_bitmask;
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

    /// Text layout debug overlay + structured log.
    /// When true, TextRunNode draws colored markers (canvas center, layout box,
    /// visual bounds, alpha centroid) and emits a compact [text-layout] log.
    /// Enabled by --debug-text-layout CLI flag.
    bool text_layout_debug{false};

    /// Text layout debug JSON export path.
    /// When non-empty and text_layout_debug is true, writes a JSON file
    /// with per-TextRun bounds data (alpha_bounds, layout_bounds,
    /// scratch_bounds, ascent, descent).
    std::string text_layout_debug_json_path;

    /**
     * WP-6 PR 6.9 — Determinism-contract safety net (default ON).
     *
     * When `true`, the non-associative float-reduction batch path in
     * `SoftwareCompositor::composite_layer_normal_optimized` is routed to
     * the scalar fallback inside `simd::composite_normal_premul(...,
     * force_scalar_normal_blend)`.  This single runtime flag controls
     * ONLY the HighLevel-Highway SIMD path under `software_compositor.cpp`
     * — it plumbs through the `simd::composite_normal_premul` call.
     *
     * The four other SIMD-relevant sites use **independent gating
     * mechanisms** and are NOT controlled by this flag:
     *
     *   * `src/backends/software/utils/blend2d_bridge_core.cpp`
     *     (composite_framebuffer — Normal-blend AVX2 batch) — gated by
     *     the COMPILE-TIME macro `CHRONON3D_FORCE_SCALAR_BLEND`, defined
     *     via `#define CHRONON3D_FORCE_SCALAR_BLEND` near the top of the
     *     TU unless `CHRONON3D_ENABLE_AVX2_BLEND` is set before include.
     *     To re-enable AVX2 perf in a benchmark-only build, define
     *     `CHRONON3D_ENABLE_AVX2_BLEND` via CMake
     *     `target_compile_definitions(<bench-target> PRIVATE
     *     CHRONON3D_ENABLE_AVX2_BLEND)`.
     *   * `src/backends/software/utils/blend2d_bridge_transforms_fb.cpp`
     *     (composite_framebuffer_transformed — scale-translation AVX2) —
     *     same `CHRONON3D_FORCE_SCALAR_BLEND` macro gate as above
     *     (defined inside its own TU).
     *   * `src/backends/software/rasterizers/path/pip.cpp`
     *     (point_in_polygon_avx2 — AVX2 PIP even-odd) — has long been
     *     dormant-by-default: the anonymous-namespace static
     *     `bool s_use_simd = false;` (line 16, pre-PR-6.9) means
     *     `set_pip_mode(true)` is the *only* way to enable it, and no
     *     production caller invokes that function at startup.  PR 6.9
     *     makes this *explicit* in the determinism contract by
     *     documenting that without a call to `set_pip_mode(true)` the
     *     AVX2 path is unreachable at runtime.
     *
     * Root-cause for the OFF-by-default switch: the rot signature tracked
     * by TICKET-007.q/r/s/t/u in
     * `tests/deterministic/gradient_determinism_tests.cpp` lines 289/314/
     * 351/391/416 was a non-associative float-reduction in those SIMD
     * batch paths triggered by `tbb::parallel_for` ordering changes.  A
     * bit-exact re-enable of those 5 tests is the gating requirement for
     * flipping the default back to `false`.  See
     * `docs/01-baseline-green.md` §3.1 ("Test Rossi — TICKET-007 fixed
     * via WP-6 PR 6.9") for the resolved-state note.
     *
     * The intended long-term fix is an *ordered* reduction (Kahan
     * summation OR sort-by-tile-id then sequential fold) for the AVX2
     * batch path — this is a separate ticket so the determinism
     * contract can be met without gating the perf win.
     */
    bool force_scalar_normal_blend{true};

    /**
     * SceneProgramCache capacity (number of entries).
     * 0 = use default (8).  Affects how many distinct nested scene programs
     * can be cached before LRU eviction kicks in.
     */
    size_t program_cache_capacity{0};

    /**
     * Enable automatic capacity tuning based on hit/eviction statistics.
     * When true, PrecompNode periodically adjusts its SceneProgramCache.
     */
    bool program_cache_tune{false};

    /**
     * Number of find_or_compile() calls between auto-tune checks.
     * Default 30 frames.
     */
    size_t program_cache_tune_interval{30};

    /**
     * Minimum capacity when down-tuning (auto mode).  Default 2.
     */
    size_t program_cache_tune_min_capacity{2};

    /**
     * Maximum capacity when up-tuning (auto mode).  Default 128.
     */
    size_t program_cache_tune_max_capacity{128};
};

} // namespace chronon3d
