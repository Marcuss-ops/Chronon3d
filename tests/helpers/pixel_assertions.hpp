#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d::test {

inline bool colors_match(Color a, Color b, float threshold = 0.05f) {
    return std::abs(a.r - b.r) < threshold &&
           std::abs(a.g - b.g) < threshold &&
           std::abs(a.b - b.b) < threshold &&
           std::abs(a.a - b.a) < threshold;
}

inline float centroid_x(const Framebuffer& fb, Color selector, float threshold = 0.05f) {
    double sum_x = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector, threshold)) {
                sum_x += x;
                count++;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_x / count) : -1.0f;
}

inline float centroid_y(const Framebuffer& fb, Color selector, float threshold = 0.05f) {
    double sum_y = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector, threshold)) {
                sum_y += y;
                count++;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_y / count) : -1.0f;
}

inline int count_pixels(const Framebuffer& fb, Color selector, float threshold = 0.05f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector, threshold)) {
                count++;
            }
        }
    }
    return count;
}

// ── Dominant-channel helpers (robust to brightness changes) ─────────────

inline int count_pixels_dominant_r(const Framebuffer& fb, float min_channel = 0.05f, float margin = 0.02f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.r > min_channel && c.r > c.g + margin && c.r > c.b + margin) {
                ++count;
            }
        }
    }
    return count;
}

inline int count_pixels_dominant_g(const Framebuffer& fb, float min_channel = 0.05f, float margin = 0.02f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.g > min_channel && c.g > c.r + margin && c.g > c.b + margin) {
                ++count;
            }
        }
    }
    return count;
}

inline int count_pixels_dominant_b(const Framebuffer& fb, float min_channel = 0.05f, float margin = 0.02f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.b > min_channel && c.b > c.r + margin && c.b > c.g + margin) {
                ++count;
            }
        }
    }
    return count;
}

inline float centroid_x_dominant_r(const Framebuffer& fb, float min_channel = 0.05f, float margin = 0.02f) {
    double sum_x = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.r > min_channel && c.r > c.g + margin && c.r > c.b + margin) {
                sum_x += x;
                ++count;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_x / count) : -1.0f;
}

inline float centroid_x_dominant_b(const Framebuffer& fb, float min_channel = 0.05f, float margin = 0.02f) {
    double sum_x = 0;
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.b > min_channel && c.b > c.r + margin && c.b > c.g + margin) {
                sum_x += x;
                ++count;
            }
        }
    }
    return count > 0 ? static_cast<float>(sum_x / count) : -1.0f;
}

inline bool any_pixel(const Framebuffer& fb, Color selector, float threshold = 0.05f) {
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector, threshold)) {
                return true;
            }
        }
    }
    return false;
}

inline Color average_color_rect(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    float r = 0, g = 0, b = 0, a = 0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            r += c.r;
            g += c.g;
            b += c.b;
            a += c.a;
            count++;
        }
    }
    if (count == 0) return Color::black();
    return Color{r / count, g / count, b / count, a / count};
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

// ── Semantic comparison helpers for determinism harness ──────────────

struct PixelDiffStats {
    float max_channel_error{0.0f};   // max |a-b| across all channels
    float mean_channel_error{0.0f}; // mean |a-b| across all channels & pixels
    float psnr{0.0f};                // PSNR in dB (0 = infinite, i.e. identical)
    int   mismatched_pixels{0};      // pixels with max_channel_error > threshold
    int   total_pixels{0};
};

inline PixelDiffStats compare_framebuffers_semantic(
    const Framebuffer& a,
    const Framebuffer& b,
    float mismatch_threshold = 3.0f / 255.0f
) {
    PixelDiffStats stats;
    stats.total_pixels = a.width() * a.height();
    if (a.width() != b.width() || a.height() != b.height() || stats.total_pixels == 0) {
        stats.max_channel_error = 1.0f;
        stats.mean_channel_error = 1.0f;
        stats.psnr = 0.0f;
        stats.mismatched_pixels = stats.total_pixels;
        return stats;
    }

    double total_diff = 0.0;
    double mse = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            Color ca = a.get_pixel(x, y);
            Color cb = b.get_pixel(x, y);
            float dr = std::abs(ca.r - cb.r);
            float dg = std::abs(ca.g - cb.g);
            float db = std::abs(ca.b - cb.b);
            float da = std::abs(ca.a - cb.a);
            float max_d = std::max({dr, dg, db, da});
            stats.max_channel_error = std::max(stats.max_channel_error, max_d);
            total_diff += dr + dg + db + da;
            mse += (dr * dr + dg * dg + db * db + da * da);
            if (max_d > mismatch_threshold) {
                ++stats.mismatched_pixels;
            }
        }
    }
    const int n = stats.total_pixels * 4;
    stats.mean_channel_error = static_cast<float>(total_diff / n);
    mse /= n;
    if (mse > 0.0) {
        stats.psnr = static_cast<float>(-10.0 * std::log10(mse));
    } else {
        stats.psnr = 120.0f; // identical
    }
    return stats;
}

// Simple SSIM over a single channel (luminance) for quick perceptual comparison.
// Returns SSIM in [0, 1] where 1 = identical.
inline float ssim_luminance(const Framebuffer& a, const Framebuffer& b) {
    if (a.width() != b.width() || a.height() != b.height()) return 0.0f;
    const int w = a.width();
    const int h = a.height();
    if (w == 0 || h == 0) return 0.0f;

    // Compute mean luminance
    double mu1 = 0.0, mu2 = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color c1 = a.get_pixel(x, y);
            Color c2 = b.get_pixel(x, y);
            mu1 += (c1.r * 0.299 + c1.g * 0.587 + c1.b * 0.114);
            mu2 += (c2.r * 0.299 + c2.g * 0.587 + c2.b * 0.114);
        }
    }
    mu1 /= (w * h);
    mu2 /= (w * h);

    double sigma1 = 0.0, sigma2 = 0.0, sigma12 = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color c1 = a.get_pixel(x, y);
            Color c2 = b.get_pixel(x, y);
            double l1 = c1.r * 0.299 + c1.g * 0.587 + c1.b * 0.114;
            double l2 = c2.r * 0.299 + c2.g * 0.587 + c2.b * 0.114;
            double d1 = l1 - mu1;
            double d2 = l2 - mu2;
            sigma1 += d1 * d1;
            sigma2 += d2 * d2;
            sigma12 += d1 * d2;
        }
    }
    const int n = w * h;
    sigma1 /= n;
    sigma2 /= n;
    sigma12 /= n;

    const double c1 = 0.01 * 0.01; // (0.01)^2 for float [0,1]
    const double c2 = 0.03 * 0.03;

    double numerator   = (2.0 * mu1 * mu2 + c1) * (2.0 * sigma12 + c2);
    double denominator = (mu1 * mu1 + mu2 * mu2 + c1) * (sigma1 + sigma2 + c2);
    if (denominator == 0.0) return 1.0f;
    return static_cast<float>(numerator / denominator);
}

// Bounding box of pixels matching a color selector.
struct BBox {
    int x0{0}, y0{0}, x1{0}, y1{0};
    bool empty{true};
    int width() const { return empty ? 0 : (x1 - x0 + 1); }
    int height() const { return empty ? 0 : (y1 - y0 + 1); }
    int area() const { return width() * height(); }
};

inline BBox bounding_box(const Framebuffer& fb, Color selector, float threshold = 0.05f) {
    BBox bb;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (colors_match(fb.get_pixel(x, y), selector, threshold)) {
                if (bb.empty) {
                    bb.x0 = bb.x1 = x;
                    bb.y0 = bb.y1 = y;
                    bb.empty = false;
                } else {
                    bb.x0 = std::min(bb.x0, x);
                    bb.x1 = std::max(bb.x1, x);
                    bb.y0 = std::min(bb.y0, y);
                    bb.y1 = std::max(bb.y1, y);
                }
            }
        }
    }
    return bb;
}

} // namespace chronon3d::test
