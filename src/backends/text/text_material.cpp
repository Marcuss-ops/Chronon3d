#include <chronon3d/text/text_material.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <blend2d.h>

namespace chronon3d {

// ── helpers ──────────────────────────────────────────────────────────

static inline uint8_t f32_to_u8(float v) {
    return static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
}

static inline float u8_to_f32(uint8_t v) {
    return static_cast<float>(v) / 255.0f;
}

static inline uint32_t rgba_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) <<  8) |
           (static_cast<uint32_t>(b));
}

static inline void rgba_unpack(uint32_t p, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    a = static_cast<uint8_t>((p >> 24) & 0xFF);
    r = static_cast<uint8_t>((p >> 16) & 0xFF);
    g = static_cast<uint8_t>((p >>  8) & 0xFF);
    b = static_cast<uint8_t>((p      ) & 0xFF);
}

// Premultiplied-alpha over composite on BL_FORMAT_PRGB32.
// dst pixels are premultiplied; src_color is straight alpha.
static inline uint32_t blend_over(uint32_t dst, const Color& src_color, float src_alpha) {
    uint8_t dr, dg, db, da;
    rgba_unpack(dst, dr, dg, db, da);

    const float da_f = u8_to_f32(da);
    const float sa = src_alpha;

    // Premultiplied over operator:
    //   out_a    = sa + da * (1 - sa)
    //   out_rgb  = src_premul + dst_premul * (1 - sa)
    // PRGB32 stores dr already premultiplied, so u8_to_f32(dr) = R_dst * da_f.
    // No division by out_a → result stays premultiplied for PRGB32.
    const float out_a = sa + da_f * (1.0f - sa);
    if (out_a < 1e-7f) return 0;

    const float out_r = src_color.r * sa + u8_to_f32(dr) * (1.0f - sa);
    const float out_g = src_color.g * sa + u8_to_f32(dg) * (1.0f - sa);
    const float out_b = src_color.b * sa + u8_to_f32(db) * (1.0f - sa);

    return rgba_pack(
        f32_to_u8(std::clamp(out_r, 0.0f, 1.0f)),
        f32_to_u8(std::clamp(out_g, 0.0f, 1.0f)),
        f32_to_u8(std::clamp(out_b, 0.0f, 1.0f)),
        f32_to_u8(std::clamp(out_a, 0.0f, 1.0f))
    );
}

// ── apply_text_material ──────────────────────────────────────────────
//
// Modifies a text BLImage in-place, applying gradient fill, bevel,
// top highlight, bottom shade, and emissive boost from the TextMaterial.
//
// The input image should contain white (or colored) text on transparent
// background. The function operates on premultiplied alpha content.

void apply_text_material(BLImage& img, const TextMaterial& mat) {
    if (!mat.enabled) return;

    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;
    if (!data.pixelData || data.size.w <= 0 || data.size.h <= 0) return;

    const int w = data.size.w;
    const int h = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    auto* pixels = static_cast<uint32_t*>(data.pixelData);

    // ── Extract alpha channel for bevel / highlight processing ──
    std::vector<float> alpha(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t a;
            uint8_t r, g, b;
            rgba_unpack(pixels[y * stride + x], r, g, b, a);
            alpha[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = u8_to_f32(a);
        }
    }

    // ── 1. Gradient fill + emissive ────────────────────────────
    float p_min = 0.0f;
    float p_max = 0.0f;
    float p_range = 1.0f;
    float cos_a = 0.0f;
    float sin_a = 1.0f;

    if (std::abs(mat.gradient_angle - 90.0f) < 1e-4f) {
        // Vertical gradient top-to-bottom
        p_min = 0.0f;
        p_max = static_cast<float>(h - 1);
        p_range = (h > 1) ? p_max : 1.0f;
        cos_a = 0.0f;
        sin_a = 1.0f;
    } else if (std::abs(mat.gradient_angle - 0.0f) < 1e-4f) {
        // Horizontal gradient left-to-right
        p_min = 0.0f;
        p_max = static_cast<float>(w - 1);
        p_range = (w > 1) ? p_max : 1.0f;
        cos_a = 1.0f;
        sin_a = 0.0f;
    } else {
        const float rad = mat.gradient_angle * (3.14159265f / 180.0f);
        cos_a = std::cos(rad);
        sin_a = std::sin(rad);
        p_min = 1e10f;
        p_max = -1e10f;
        for (float cy : {0.0f, static_cast<float>(h - 1)}) {
            for (float cx : {0.0f, static_cast<float>(w - 1)}) {
                float proj = cx * cos_a + cy * sin_a;
                p_min = std::min(p_min, proj);
                p_max = std::max(p_max, proj);
            }
        }
        p_range = (p_max > p_min + 1e-5f) ? (p_max - p_min) : 1.0f;
    }

    const float emissive = mat.emissive;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = alpha[y * w + x];
            if (a <= 0.0f) {
                pixels[y * stride + x] = 0;
                continue;
            }

            float proj = static_cast<float>(x) * cos_a + static_cast<float>(y) * sin_a;
            float t = std::clamp((proj - p_min) / p_range, 0.0f, 1.0f);
            const Color grad_color = mat.top_color * (1.0f - t) + mat.bottom_color * t;

            // Apply gradient color using the original alpha as mask
            float r = grad_color.r * emissive;
            float g = grad_color.g * emissive;
            float b = grad_color.b * emissive;

            // Clamp
            r = std::clamp(r, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b, 0.0f, 1.0f);

            pixels[y * stride + x] = rgba_pack(
                f32_to_u8(r),
                f32_to_u8(g),
                f32_to_u8(b),
                f32_to_u8(a)
            );
        }
    }

    // ── 2. Bevel (fake 3D edge) ────────────────────────────────
    // Uses separable sliding-window maximum (deque per row/column)
    // for O(w×h) complexity instead of O(w×h×bp) naive scanning.
    if (mat.bevel_px > 0.0f) {
        const int bp = static_cast<int>(std::round(mat.bevel_px));
        if (bp > 0) {
            const int window = bp * 2;
            const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);

            // Horizontal sliding-window maximum
            std::vector<float> h_max(n);
            for (int y = 0; y < h; ++y) {
                std::deque<int> dq;
                const size_t row_base = static_cast<size_t>(y) * static_cast<size_t>(w);
                for (int x = 0; x < w; ++x) {
                    while (!dq.empty() && dq.front() < x - window) dq.pop_front();
                    const float val = alpha[row_base + static_cast<size_t>(x)];
                    while (!dq.empty() && alpha[row_base + static_cast<size_t>(dq.back())] <= val) dq.pop_back();
                    dq.push_back(x);
                    h_max[row_base + static_cast<size_t>(x)] = alpha[row_base + static_cast<size_t>(dq.front())];
                }
            }

            // Vertical sliding-window maximum
            std::vector<float> v_max(n);
            for (int x = 0; x < w; ++x) {
                std::deque<int> dq;
                const size_t col = static_cast<size_t>(x);
                for (int y = 0; y < h; ++y) {
                    while (!dq.empty() && dq.front() < y - window) dq.pop_front();
                    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + col;
                    const float val = alpha[idx];
                    while (!dq.empty() && alpha[static_cast<size_t>(dq.back()) * static_cast<size_t>(w) + col] <= val) dq.pop_back();
                    dq.push_back(y);
                    v_max[idx] = alpha[static_cast<size_t>(dq.front()) * static_cast<size_t>(w) + col];
                }
            }

            // Single-pass edge detection using precomputed maxima
            for (int y = 0; y < h; ++y) {
                const size_t row_base = static_cast<size_t>(y) * static_cast<size_t>(w);
                for (int x = 0; x < w; ++x) {
                    const float orig_a = alpha[row_base + static_cast<size_t>(x)];
                    if (orig_a <= 0.0f) continue;

                    // ── Highlight edges (top + left) ──────────────
                    const int py_top = y - bp;
                    const int px_left = x - bp;
                    float hl_strength = 0.0f;

                    if (py_top >= 0) {
                        const float above_max = h_max[static_cast<size_t>(py_top) * static_cast<size_t>(w) + static_cast<size_t>(x)];
                        if (above_max < orig_a * 0.5f) {
                            hl_strength = std::max(hl_strength,
                                (orig_a - above_max) / orig_a);
                        }
                    } else {
                        hl_strength = std::max(hl_strength, 1.0f);
                    }

                    if (px_left >= 0) {
                        const float left_max = v_max[row_base + static_cast<size_t>(px_left)];
                        if (left_max < orig_a * 0.5f) {
                            hl_strength = std::max(hl_strength,
                                (orig_a - left_max) / orig_a);
                        }
                    } else {
                        hl_strength = std::max(hl_strength, 1.0f);
                    }

                    if (hl_strength > 0.1f) {
                        const float hl_alpha = hl_strength * mat.bevel_highlight_opacity * orig_a;
                        pixels[y * stride + x] = blend_over(
                            pixels[y * stride + x],
                            mat.bevel_highlight_color,
                            hl_alpha
                        );
                    }

                    // ── Shadow edges (bottom + right) ─────────────
                    const int py_bottom = y + bp;
                    const int px_right = x + bp;
                    float sh_strength = 0.0f;

                    if (py_bottom < h) {
                        const float below_max = h_max[static_cast<size_t>(py_bottom) * static_cast<size_t>(w) + static_cast<size_t>(x)];
                        if (below_max < orig_a * 0.5f) {
                            sh_strength = std::max(sh_strength,
                                (orig_a - below_max) / orig_a);
                        }
                    } else {
                        sh_strength = std::max(sh_strength, 1.0f);
                    }

                    if (px_right < w) {
                        const float right_max = v_max[row_base + static_cast<size_t>(px_right)];
                        if (right_max < orig_a * 0.5f) {
                            sh_strength = std::max(sh_strength,
                                (orig_a - right_max) / orig_a);
                        }
                    } else {
                        sh_strength = std::max(sh_strength, 1.0f);
                    }

                    if (sh_strength > 0.1f) {
                        const float sh_alpha = sh_strength * mat.bevel_shadow_opacity * orig_a;
                        const Color shadow_tint{0.0f, 0.0f, 0.0f, 1.0f};
                        pixels[y * stride + x] = blend_over(
                            pixels[y * stride + x],
                            shadow_tint,
                            sh_alpha
                        );
                    }
                }
            }
        }
    }

    // ── 3. Top highlight strip ─────────────────────────────────
    if (mat.top_highlight_opacity > 0.0f && mat.top_highlight_fraction > 0.0f) {
        const int hl_height = std::max(1, static_cast<int>(static_cast<float>(h) * mat.top_highlight_fraction));
        for (int y = 0; y < hl_height && y < h; ++y) {
            const float t = 1.0f - static_cast<float>(y) / static_cast<float>(hl_height);
            const float fade = t * t; // quadratic fade
            for (int x = 0; x < w; ++x) {
                const float a = alpha[y * w + x];
                if (a <= 0.0f) continue;
                const float hl_a = fade * mat.top_highlight_opacity * a;
                pixels[y * stride + x] = blend_over(
                    pixels[y * stride + x],
                    Color{1.0f, 1.0f, 1.0f, 1.0f},
                    hl_a
                );
            }
        }
    }

    // ── 4. Bottom shade strip ──────────────────────────────────
    if (mat.bottom_shade_opacity > 0.0f && mat.bottom_shade_fraction > 0.0f) {
        const int sh_height = std::max(1, static_cast<int>(static_cast<float>(h) * mat.bottom_shade_fraction));
        for (int y = std::max(0, h - sh_height); y < h; ++y) {
            const float t = static_cast<float>(y - (h - sh_height)) / static_cast<float>(sh_height);
            const float fade = t * t; // quadratic fade
            for (int x = 0; x < w; ++x) {
                const float a = alpha[y * w + x];
                if (a <= 0.0f) continue;
                const float sh_a = fade * mat.bottom_shade_opacity * a;
                pixels[y * stride + x] = blend_over(
                    pixels[y * stride + x],
                    Color{0.0f, 0.0f, 0.0f, 1.0f},
                    sh_a
                );
            }
        }
    }

    // ── 5. Inner Shadow ────────────────────────────────────────
    if (mat.inner_shadow_enabled) {
        auto box_blur = [](const std::vector<float>& src, std::vector<float>& dst, int w, int h, float radius) {
            int r = static_cast<int>(std::round(radius));
            if (r <= 0) {
                dst = src;
                return;
            }
            std::vector<float> tmp(src.size());
            // Horizontal pass
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float sum = 0.0f;
                    int count = 0;
                    for (int k = -r; k <= r; ++k) {
                        int nx = x + k;
                        if (nx >= 0 && nx < w) {
                            sum += src[y * w + nx];
                            count++;
                        }
                    }
                    tmp[y * w + x] = sum / count;
                }
            }
            // Vertical pass
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float sum = 0.0f;
                    int count = 0;
                    for (int k = -r; k <= r; ++k) {
                        int ny = y + k;
                        if (ny >= 0 && ny < h) {
                            sum += tmp[ny * w + x];
                            count++;
                        }
                    }
                    dst[y * w + x] = sum / count;
                }
            }
        };

        auto sample_inverted_alpha = [](int x, int y, float dx, float dy, const std::vector<float>& alpha, int w, int h) {
            int sx = static_cast<int>(std::round(x - dx));
            int sy = static_cast<int>(std::round(y - dy));
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                return 1.0f - alpha[sy * w + sx];
            }
            return 1.0f;
        };

        std::vector<float> inner_shadow_src(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                inner_shadow_src[y * w + x] = sample_inverted_alpha(x, y, mat.inner_shadow_offset.x, mat.inner_shadow_offset.y, alpha, w, h);
            }
        }

        std::vector<float> inner_shadow_blurred(w * h);
        box_blur(inner_shadow_src, inner_shadow_blurred, w, h, mat.inner_shadow_blur);

        // Blend inner shadow color inside the glyph bounds
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float orig_a = alpha[y * w + x];
                if (orig_a <= 0.0f) continue;

                const float shadow_opacity = inner_shadow_blurred[y * w + x] * mat.inner_shadow_color.a * orig_a;
                if (shadow_opacity > 0.0f) {
                    pixels[y * stride + x] = blend_over(
                        pixels[y * stride + x],
                        mat.inner_shadow_color,
                        shadow_opacity
                    );
                }
            }
        }
    }
}

} // namespace chronon3d
