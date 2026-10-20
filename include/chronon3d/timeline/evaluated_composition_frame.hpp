#pragma once

#include <memory_resource>

#include <chronon3d/scene/model/core/scene.hpp>

namespace chronon3d {

/// The scene produced by evaluating a composition at one sample time.
/// Its camera is already resolved and stored in Scene for the renderer.
struct EvaluatedCompositionFrame {
    explicit EvaluatedCompositionFrame(
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : scene(resource) {}

    Scene scene{};
};

} // namespace chronon3d
