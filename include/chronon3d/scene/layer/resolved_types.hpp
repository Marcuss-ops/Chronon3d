#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

struct ResolvedLayer {
    const Layer* layer{nullptr};
    Transform world_transform{};
    usize insertion_index{0};
    bool has_parent{false};
    bool parent_missing{false};
    bool cycle_detected{false};
};

struct ResolvedCamera {
    Camera2_5DRuntime camera;
    Transform world_transform;
};

} // namespace chronon3d
