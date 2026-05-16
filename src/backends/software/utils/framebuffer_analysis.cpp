#include <chronon3d/backends/software/framebuffer_analysis.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

bool matrix_near(const Mat4& a, const Mat4& b, float eps) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a[c][r] - b[c][r]) > eps) {
                return false;
            }
        }
    }
    return true;
}

std::optional<BrightRegion2D> bright_bbox(const Framebuffer& fb, float threshold) {
    BrightRegion2D bbox;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color p = fb.get_pixel(x, y);
            if ((p.r + p.g + p.b) <= threshold) continue;
            bbox.min_x = std::min(bbox.min_x, x);
            bbox.min_y = std::min(bbox.min_y, y);
            bbox.max_x = std::max(bbox.max_x, x);
            bbox.max_y = std::max(bbox.max_y, y);
        }
    }

    if (bbox.max_x < bbox.min_x || bbox.max_y < bbox.min_y) {
        return std::nullopt;
    }
    return bbox;
}

std::optional<Vec2> bright_centroid(const Framebuffer& fb, float threshold) {
    double sum_x = 0.0;
    double sum_y = 0.0;
    double count = 0.0;

    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color p = fb.get_pixel(x, y);
            if ((p.r + p.g + p.b) <= threshold) continue;
            sum_x += x;
            sum_y += y;
            count += 1.0;
        }
    }

    if (count <= 0.0) return std::nullopt;
    return Vec2{static_cast<float>(sum_x / count), static_cast<float>(sum_y / count)};
}

} // namespace chronon3d::renderer
