#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

namespace chronon3d {

struct CompositionDefinition {
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    CompositionSpec composition{};
    SceneFunction scene{};
    std::uint64_t scene_content_fingerprint{0};
    std::optional<camera_v1::CameraDescriptor> camera{};
    bool scene_is_frame_invariant{false};
};

} // namespace chronon3d
