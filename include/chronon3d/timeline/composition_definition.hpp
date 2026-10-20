#pragma once

#include <cstdint>
#include <functional>

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {

struct CompositionDefinition {
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    CompositionSpec composition{};
    SceneFunction scene{};
    std::uint64_t scene_content_fingerprint{0};
    bool scene_is_frame_invariant{false};
};

} // namespace chronon3d
