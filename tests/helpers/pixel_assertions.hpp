#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>

namespace chronon3d::test {

inline bool colors_match(Color a, Color b, float threshold = 0.05f) {
    return std::abs(a.r - b.r) < threshold &&
           std::abs(a.g - b.g) < threshold &&
           std::abs(a.b - b.b) < threshold &&
           std::abs(a.a - b.a) < threshold;
}

inline float centroid_x(const Framebuffer& fb, Color selector) {
    double sum_x = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector)) {
                sum_x += x;
                count++;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_x / count) : -1.0f;
}

inline float centroid_y(const Framebuffer& fb, Color selector) {
    double sum_y = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector)) {
                sum_y += y;
                count++;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_y / count) : -1.0f;
}

inline int count_pixels(const Framebuffer& fb, Color selector) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector)) {
                count++;
            }
        }
    }
    return count;
}

inline bool any_pixel(const Framebuffer& fb, Color selector) {
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector)) {
                return true;
            }
        }
    }
    return false;
}

inline int width_at_row(const Framebuffer& fb, int y, Color selector) {
    if (y < 0 || y >= fb.height()) return 0;
    int first = -1;
    int last = -1;
    for (int x = 0; x < fb.width(); ++x) {
        if (colors_match(fb.get_pixel(x, y), selector)) {
            if (first == -1) first = x;
            last = x;
        }
    }
    return first == -1 ? 0 : (last - first + 1);
}

inline int height_at_col(const Framebuffer& fb, int x, Color selector) {
    if (x < 0 || x >= fb.width()) return 0;
    int first = -1;
    int last = -1;
    for (int y = 0; y < fb.height(); ++y) {
        if (colors_match(fb.get_pixel(x, y), selector)) {
            if (first == -1) first = y;
            last = y;
        }
    }
    return first == -1 ? 0 : (last - first + 1);
}

} // namespace chronon3d::test
