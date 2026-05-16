#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/vec2.hpp>

#include <optional>

namespace chronon3d::renderer {

struct BrightRegion2D {
    int min_x{0};
    int min_y{0};
    int max_x{-1};
    int max_y{-1};
};

bool matrix_near(const Mat4& a, const Mat4& b, float eps = 1e-4f);
std::optional<BrightRegion2D> bright_bbox(const Framebuffer& fb, float threshold = 0.1f);
std::optional<Vec2> bright_centroid(const Framebuffer& fb, float threshold = 0.1f);

} // namespace chronon3d::renderer
