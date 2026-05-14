#pragma once

#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d {

struct RenderSettings {
    /**
     * Deprecated — kept for CLI/test compatibility. Has no effect.
     * The rendergraph pipeline is now the only execution path.
     */
    bool use_modular_graph{false};

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
     * If empty, a default path or built-in fallback is used.
     */
    std::string diagnostic_font_path;

};

} // namespace chronon3d
