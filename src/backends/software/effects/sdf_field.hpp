// ---------------------------------------------------------------------------
// sdf_field.hpp — shared software signed-distance field builder
//
// Builds one field from a Framebuffer alpha silhouette using the exact
// separable Felzenszwalb & Huttenlocher transform already used by SdfBevel.
// Effects that need contour bands or edge normals include this header instead
// of carrying a second distance-transform implementation.
// ---------------------------------------------------------------------------

#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::renderer::sdf_field {

constexpr float kDistanceInfinity = 1.0e20f;

// 1D squared Euclidean distance transform. Infinite sites are skipped so
// rows/columns that contain no seed contribute no candidates instead of
// producing NaNs.
inline void distance_transform_1d(const float* f, float* d, int n) {
    if (n <= 0) return;
    std::vector<int> v(static_cast<std::size_t>(n));
    std::vector<float> z(static_cast<std::size_t>(n) + 1);
    int k = -1;

    for (int q = 0; q < n; ++q) {
        if (!std::isfinite(f[q])) continue;
        if (k < 0) {
            k = 0;
            v[0] = q;
            z[0] = -kDistanceInfinity;
            z[1] = kDistanceInfinity;
            continue;
        }
        float s = ((f[q] + static_cast<float>(q) * q) -
                   (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                  (2.0f * static_cast<float>(q - v[k]));
        while (s <= z[k]) {
            --k;
            if (k < 0) break;
            s = ((f[q] + static_cast<float>(q) * q) -
                 (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                (2.0f * static_cast<float>(q - v[k]));
        }
        if (k < 0) {
            k = 0;
            v[0] = q;
            z[0] = -kDistanceInfinity;
            z[1] = kDistanceInfinity;
        } else {
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = kDistanceInfinity;
        }
    }

    if (k < 0) {
        for (int q = 0; q < n; ++q) d[q] = kDistanceInfinity;
        return;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q)) ++k;
        const float dq = static_cast<float>(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

inline void distance_transform_2d(std::vector<float>& field, int w, int h) {
    std::vector<float> line(static_cast<std::size_t>(std::max(w, h)));
    std::vector<float> out(static_cast<std::size_t>(std::max(w, h)));

    for (int y = 0; y < h; ++y) {
        float* row = field.data() + static_cast<std::size_t>(y) * w;
        distance_transform_1d(row, out.data(), w);
        std::copy(out.begin(), out.begin() + w, row);
    }

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            line[y] = field[static_cast<std::size_t>(y) * w + x];
        }
        distance_transform_1d(line.data(), out.data(), h);
        for (int y = 0; y < h; ++y) {
            field[static_cast<std::size_t>(y) * w + x] = out[y];
        }
    }
}

struct Field {
    int width{0};
    int height{0};
    bool has_content{false};
    std::vector<float> values;

    [[nodiscard]] bool empty() const noexcept {
        return width <= 0 || height <= 0 || values.empty();
    }

    [[nodiscard]] float at(int x, int y) const noexcept {
        return values[static_cast<std::size_t>(y) * width + x];
    }
};

// Negative inside the silhouette, positive outside. The threshold matches the
// existing SdfBevel alpha cut (0.5); callers can override it for softer source
// mattes while keeping the same distance transform.
inline Field build_from_alpha(const Framebuffer& fb, float alpha_threshold = 0.5f) {
    Field result;
    result.width = fb.width();
    result.height = fb.height();
    if (result.width <= 0 || result.height <= 0) return result;

    const std::size_t count =
        static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    std::vector<float> to_inside(count, kDistanceInfinity);
    std::vector<float> to_outside(count, kDistanceInfinity);

    for (int y = 0; y < result.height; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < result.width; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(y) * result.width + x;
            if (row[x].a > alpha_threshold) {
                to_inside[i] = 0.0f;
                result.has_content = true;
            } else {
                to_outside[i] = 0.0f;
            }
        }
    }
    if (!result.has_content) return result;

    distance_transform_2d(to_inside, result.width, result.height);
    distance_transform_2d(to_outside, result.width, result.height);
    result.values.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.values[i] = std::sqrt(to_inside[i]) - std::sqrt(to_outside[i]);
    }
    return result;
}

} // namespace chronon3d::renderer::sdf_field
