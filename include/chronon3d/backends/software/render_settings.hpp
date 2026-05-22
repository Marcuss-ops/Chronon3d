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
     * Path to the font used for diagnostic overlays.
     */
    std::string diagnostic_font_path;

    /**
     * Tiling size (e.g. 256 or 512). 0 = disabled.
     */
    int tile_size{0};

    /**
     * If true, enables in-place composition to avoid costly framebuffer copies.
     */
    bool optimize_compositing{true};

    /**
     * If true, enables dirty rectangles partial invalidation optimization.
     */
    bool enable_dirty_rects{true};

    /**
     * If true, graph nodes may restrict clear/composite/effect work to predicted bounds.
     * V1 uses node predicted_bbox as clip_rect.
     */
    bool dirty_rects{false};
};

} // namespace chronon3d
