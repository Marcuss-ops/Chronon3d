#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <string>
#include <optional>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {

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

    /**
     * If true, enables diagnostic overlays and info in the output.
     */
    bool diagnostic{false};

    /**
     * If true, logs a graph preflight report before rendering each frame.
     */
    bool diagnostic_plan{false};

    /**
     * Optional path pattern for the graph preflight report output.
     * If no #### placeholder is present, a _0000 frame suffix is inserted.
     */
    std::string diagnostic_plan_output;

    /**
     * Path to the font used for diagnostic overlays.
     */
    std::string diagnostic_font_path;

    /**
     * Tiling size (e.g. 256 or 512). 0 = disabled.
     */
    int tile_size{0};

    /**
     * If true, dirty tiles are processed in parallel via tbb::parallel_for.
     * When false, tiles are processed sequentially in a single-threaded for loop.
     * Only has effect when tile-based execution is active (use_tile_execution).
     */
    bool enable_parallel_tiles{true};

    /**
     * Maximum dirty ratio (0.0–1.0) that still enables tile-based execution.
     * When the fraction of dirty screen pixels exceeds this threshold, tile
     * execution is skipped and single-pass rendering is used instead, because
     * the overhead of per-tile graph re-execution outweighs the benefit of
     * skipping clean tiles.
     *
     * Example: 0.30 means "skip tile execution when >30% of the screen is dirty".
     * Set to 1.0 to always use tiles (no automatic throttle).
     */
    double tile_dirty_ratio_threshold{0.30};

    /**
     * If true, enables in-place composition to avoid costly framebuffer copies.
     */
    bool optimize_compositing{true};

    /**
     * If true, enables dirty rectangles partial invalidation optimization.
     */
    bool enable_dirty_rects{true};

    /**
     * If true, enables dirty rect bitmask (64x64 grid) for precise invalidation.
     */
    bool enable_dirty_bitmask{true};

    /**
     * If true, graph nodes may restrict clear/composite/effect work to predicted bounds.
     * V1 uses node predicted_bbox as clip_rect.
     */
    bool dirty_rects{false};
};

} // namespace chronon3d
