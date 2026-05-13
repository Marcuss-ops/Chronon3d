#pragma once

#include <chronon3d/scene/camera_2_5d.hpp>

namespace chronon3d {

struct RenderSettings {
    /**
     * If true, uses the new modular graph system (GraphBuilder/GraphExecutor).
     * If false, uses the legacy rendergraph pipeline.
     */
    bool use_modular_graph{false};

    /**
     * Global motion blur settings.
     */
    MotionBlurSettings motion_blur{};

    /**
     * If true, enables diagnostic overlays and info in the output.
     */
    bool diagnostic{false};
};

} // namespace chronon3d
