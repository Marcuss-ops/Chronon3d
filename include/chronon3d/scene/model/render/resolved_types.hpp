#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

struct ResolvedLayer {
    const Layer* layer{nullptr};
    Transform world_transform{};
    Mat4 world_matrix{1.0f};
    usize insertion_index{0};
    bool has_parent{false};
    bool parent_missing{false};
    bool cycle_detected{false};
};

} // namespace chronon3d
