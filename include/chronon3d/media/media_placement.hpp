#pragma once

#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

enum class FitMode {
    Stretch,
    Contain,
    Cover,
    None
};

struct Rect {
    Vec2 origin{0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
};

struct MediaPlacementResult {
    Rect src_rect;
    Rect dst_rect;
};

[[nodiscard]] MediaPlacementResult compute_media_placement(
    Vec2 source_size,
    Vec2 box_size,
    FitMode fit,
    Vec2 focal_point = {0.5f, 0.5f}
);

} // namespace chronon3d
