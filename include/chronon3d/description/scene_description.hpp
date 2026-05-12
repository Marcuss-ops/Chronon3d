#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/time.hpp>
#include <chronon3d/description/layer_desc.hpp>
#include <chronon3d/description/camera_desc.hpp>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// Top-level scene description: declares what exists in the scene.
// Evaluation against a specific frame is done by TimelineEvaluator.
// The legacy SceneFunction (FrameContext -> Scene) coexists with this.
struct SceneDescription {
    std::string name;
    i32         width{1280};
    i32         height{720};
    FrameRate   frame_rate{30, 1};
    Frame       duration{0};

    std::vector<LayerDesc>          layers;
    std::optional<Camera2_5DDesc>   camera;
};

} // namespace chronon3d
