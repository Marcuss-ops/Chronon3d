#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec3.hpp>

namespace chronon3d {

struct Camera2_5D {
    bool enabled{false};

    // Camera position in screen-space world coordinates.
    // Default: 1000 units "in front" of the z=0 plane.
    Vec3 position{0.0f, 0.0f, -1000.0f};

    // Stored for future two-node camera support; not used for orbit in v1.
    Vec3 point_of_interest{0.0f, 0.0f, 0.0f};

    // Perspective strength. At depth == zoom, perspective_scale == 1.
    f32 zoom{1000.0f};
};

} // namespace chronon3d
