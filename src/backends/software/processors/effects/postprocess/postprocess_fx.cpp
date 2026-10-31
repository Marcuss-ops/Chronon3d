#include "postprocess_fx.hpp"

#include <chronon3d/core/hash/hash_primitives.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace chronon3d::renderer {
namespace {

float hash_01(float a, float b, std::uint32_t seed) {
    std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::int32_t>(a * 1024.0f) & 0x7fffff);
    h = h * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(static_cast<std::int32_t>(b * 1024.0f) & 0x7fffff);
    h = h * 0xBF58476D1CE4E5B9ULL + seed;
    h = h * 0x94D049BB133111EBULL + 0x165667B19E3779F9ULL;
    return static_cast<float>(core::hash::splitmix64_finalize(h) >> 40) /
           static_cast<float>(0xFFFFFFULL);
}

// 1 - |2t - 1| triangle wave in [0, 1], used for the scanline luminance dips
// so the modulation stays band-limited and modern-subtle.
float triangle_wave(float t) {
    const float x = t - std::floor(t);
    return std::fabs(x * 2.0f - 1.0f);
}

Color clamp_color(const Color& c) {
    return Color{
        std::clamp(c.r, 0.0f, 1.0f),
        std::clamp(c.g, 0.0f, 1.0f),
        std::clamp(c.b, 0.0f, 1.0f),
        std::clamp(c.a, 0.0f, 1.0f),
    };
}

// Rec-601-ish component decode/encode helpers for the retro-TV chroma path.
constexpr float kLumaR = 0.299f;
constexpr float kLumaG = 0.587f;
constexpr float kLumaB = 0.114f;

struct YCbCr { float y{0.0f}, cb{0.0f}, cr{0.0f}; };

YCbCr to_ycbcr(const Color& c) {
    const float y = kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
    YCbCr out;
    out.y = y;
    out.cb = (c.b - y) * 0.564f;
    out.cr = (c.r - y) * 0.713f;
    return out;
}

Color from_ycbcr(const YCbCr& v) {
    const float r = v.y + 1.402f * v.cr;
    const float b = v.y + 1.772f * v.cb;
    const float g = (v.y - kLumaR * r - kLumaB * b) / kLumaG;
    return Color{r, g, b, 1.0f};
}

// Box-average a horizontal window of the snapshot into a point sample.
Color horizontal_average(const Framebuffer& src, float x, int y, int radius_px, int taps) {
    const int w = src.width();
    int base = static_cast<int>(std::floor(x));
    const float frac = x - static_cast<float>(base);
    Color acc{0.0f, 0.0f, 0.0f, 0.0f};
    const int step = std::max(1, static_cast<int>((2 * radius_px + taps - 1) / taps));
    int used = 0;
    for (int d = -radius_px; d <= radius_px; d += step) {
        const int sx = std::clamp(base + d, 0, w - 1);
        const Color& c = src.pixels_row(y)[sx];
        acc.r += c.r; acc.g += c.g; acc.b += c.b; acc.a += c.a;
        ++used;
    }
    if (used == 0) return src.pixels_row(y)[std::clamp(base, 0, w - 1)];
    const float inv = 1.0f / static_cast<float>(used);
    return Color{acc.r * inv, acc.g * inv, acc.b * inv, acc.a * inv};
}

} // namespace

// ── Scanlines ───────────────────────────────────────────────────────────────

void apply_scanlines(Framebuffer& fb, const ScanlinesParams& params,
                     const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.spacing_px) || params.spacing_px < 1.0f || params.spacing_px > 64.0f ||
        !std::isfinite(params.amount) || params.amount < 0.0f || params.amount > 1.0f ||
        !std::isfinite(params.phase) || params.phase < 0.0f || params.phase >= 1.0f ||
        params.axis > 1 || fb.width() <= 0 || fb.height() <= 0)
        throw std::invalid_argument("scanlines parameters or target surface are invalid");

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    const float pitch = std::max(params.spacing_px, 1.0f);
    const bool repeat_y = params.axis == 0;

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        const float row_coord = static_cast<float>(y);
        for (int x = x0; x < x1; ++x) {
            // Line coordinate along the repeated axis; the triangle wave dips
            // light at every line boundary and stays gentle — modern subtle
            // scanlines, 3-8% modulation at the default amount.
            const float line_coord = repeat_y ? row_coord : static_cast<float>(x);
            const float t = (line_coord + params.phase * pitch) / pitch;
            const float scan_factor = 1.0f - params.amount * triangle_wave(t);
            Color& c = row[x];
            c.r *= scan_factor;
            c.g *= scan_factor;
            c.b *= scan_factor;
            c = clamp_color(c);
        }
    }
}

// ── Retro TV ────────────────────────────────────────────────────────────────

void apply_retro_tv(Framebuffer& fb, const RetroTvParams& params, float time_seconds,
                    ::chronon3d::EffectScratchResources& scratch,
                    const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.jitter_px) || params.jitter_px < 0.0f || params.jitter_px > 32.0f ||
        !std::isfinite(params.jitter_speed) || params.jitter_speed < 0.0f || params.jitter_speed > 120.0f ||
        !std::isfinite(params.smear_px) || params.smear_px < 0.0f || params.smear_px > 32.0f ||
        !std::isfinite(params.bleed_strength) || params.bleed_strength < 0.0f || params.bleed_strength > 1.0f ||
        !std::isfinite(params.luma_noise) || params.luma_noise < 0.0f || params.luma_noise > 1.0f ||
        fb.width() <= 0 || fb.height() <= 0)
        throw std::invalid_argument("retro TV parameters or target surface are invalid");

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    scratch.ensure_size(w, h);
    Framebuffer& snapshot = *scratch.framebuffer;
    snapshot.blit(fb, 0, 0);

    // Deterministic tape phase: two interleaved wave bands per frame, like the
    // NTSC horizontal sync wobble — quantize time so identical inputs (same
    // quantized frame time + row + seed) reproduce identical output.
    const float quantum = params.jitter_speed > 0.0f ? 1.0f / params.jitter_speed : 0.0f;
    const float tape_phase = params.jitter_speed > 0.0f
        ? std::floor(time_seconds * params.jitter_speed) * quantum : 0.0f;
    const float smear_radius = std::floor(params.smear_px * 0.5f);
    const int taps = smear_radius > 0.0f
        ? std::clamp(static_cast<int>(std::max(2.0f, std::min(2.0f * smear_radius, 32.0f))), 2, 32) : 1;

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        const float band = hash_01(tape_phase, static_cast<float>(y) * 0.0625f, params.seed);
        const float band2 = hash_01(tape_phase, static_cast<float>(y) * 0.0625f + 7.0f, params.seed ^ 0x515A5E5u);
        // Two-weight jitter: dominant per-band wobble with occasional line skips.
        float jitter = (band - 0.5f) * 2.0f * params.jitter_px * (band2 > 0.82f ? 2.5f : 1.0f);
        jitter = std::clamp(jitter, -params.jitter_px * 2.5f, params.jitter_px * 2.5f);
        for (int x = x0; x < x1; ++x) {
            const float src_x = std::clamp(static_cast<float>(x) + jitter, 0.0f, static_cast<float>(w - 1));
            const Color sharp = snapshot.pixels_row(y)[std::clamp(
                static_cast<int>(std::lround(src_x)), 0, w - 1)];

            Color result;
            if (smear_radius > 0.0f && params.bleed_strength > 0.0f) {
                const YCbCr sharp_yuv = to_ycbcr(sharp);
                const Color blot = horizontal_average(snapshot, src_x, y,
                                                      static_cast<int>(smear_radius), taps);
                const YCbCr blurry_yuv = to_ycbcr(blot);
                const float k = params.bleed_strength;
                YCbCr mixed;
                mixed.y = sharp_yuv.y; // luma stays at full spatial resolution
                mixed.cb = blurry_yuv.cb * k + sharp_yuv.cb * (1.0f - k);
                mixed.cr = blurry_yuv.cr * k + sharp_yuv.cr * (1.0f - k);
                result = from_ycbcr(mixed);
                result.a = sharp.a;
            } else {
                result = sharp;
            }

            if (params.luma_noise > 0.0f) {
                const float noise = hash_01(static_cast<float>(x) * 0.125f + src_x,
                                            tape_phase + static_cast<float>(y) * 0.03125f,
                                            params.seed ^ 0xA5A5A5A5u) - 0.5f;
                const float y_shift = params.luma_noise * 0.35f * noise * 2.0f;
                result.r = std::clamp(result.r + y_shift, 0.0f, 1.0f);
                result.g = std::clamp(result.g + y_shift, 0.0f, 1.0f);
                result.b = std::clamp(result.b + y_shift, 0.0f, 1.0f);
            }
            row[x] = clamp_color(result);
        }
    }
}

// ── Film halation ───────────────────────────────────────────────────────────

void apply_halation(Framebuffer& fb, const HalationParams& params,
                    ::chronon3d::EffectScratchResources& scratch,
                    const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.threshold) || params.threshold < 0.0f || params.threshold > 1.0f ||
        !std::isfinite(params.softness) || params.softness < 0.0f || params.softness > 1.0f ||
        !std::isfinite(params.radius_px) || params.radius_px < 0.0f || params.radius_px > 64.0f ||
        !std::isfinite(params.intensity) || params.intensity < 0.0f || params.intensity > 8.0f ||
        fb.width() <= 0 || fb.height() <= 0)
        throw std::invalid_argument("film halation parameters or target surface are invalid");

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    scratch.ensure_size(w, h);
    Framebuffer& mask = *scratch.framebuffer;
    Framebuffer& blit_b = *scratch.framebuffer_b;
    mask.clear({0.0f, 0.0f, 0.0f, 0.0f});

    // 1. Highlight mask above threshold with a soft knee.
    const float knee = std::max(params.softness, 1.0e-3f);
    for (int y = 0; y < h; ++y) {
        const Color* src_row = fb.pixels_row(y);
        Color* dst_row = mask.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const Color& c = src_row[x];
            const float luma = kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
            const float t = std::clamp((luma - params.threshold) / knee, 0.0f, 1.0f);
            const float m = t * t * (3.0f - 2.0f * t); // smoothstep
            dst_row[x] = Color{m, m, m, m};
        }
    }

    // 2. Separable box blur of the warm mask (radius_px) with clamped edges.
    const int radius = static_cast<int>(std::lround(params.radius_px));
    if (radius > 0) {
        for (int y = 0; y < h; ++y) {
            const Color* src_row = mask.pixels_row(y);
            Color* dst_row = blit_b.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                Color acc{0.0f, 0.0f, 0.0f, 0.0f};
                int used = 0;
                for (int tx = -radius; tx <= radius; tx += std::max(1, (2 * radius) / 32)) {
                    const int sx = std::clamp(x + tx, 0, w - 1);
                    acc.r += src_row[sx].r; acc.g += src_row[sx].g;
                    acc.b += src_row[sx].b; acc.a += src_row[sx].a;
                    ++used;
                }
                const float inv = 1.0f / static_cast<float>(std::max(used, 1));
                dst_row[x] = Color{acc.r * inv, acc.g * inv, acc.b * inv, acc.a * inv};
            }
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                Color acc{0.0f, 0.0f, 0.0f, 0.0f};
                int used = 0;
                for (int ty = -radius; ty <= radius; ty += std::max(1, (2 * radius) / 32)) {
                    const int sy = std::clamp(y + ty, 0, h - 1);
                    const Color& c = blit_b.pixels_row(sy)[x];
                    acc.r += c.r; acc.g += c.g; acc.b += c.b; acc.a += c.a;
                    ++used;
                }
                const float inv = 1.0f / static_cast<float>(std::max(used, 1));
                mask.pixels_row(y)[x] = Color{acc.r * inv, acc.g * inv, acc.b * inv, acc.a * inv};
            }
        }
    }

    // 3. Screen composite of the warm bloom back onto the original.
    const float k = params.intensity;
    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const Color& m = mask.pixels_row(y)[x];
            if (m.r <= 0.0f && m.g <= 0.0f && m.b <= 0.0f) continue;
            const float halo_r = std::clamp(m.r * params.tint.r * k, 0.0f, 1.0f);
            const float halo_g = std::clamp(m.g * params.tint.g * k, 0.0f, 1.0f);
            const float halo_b = std::clamp(m.b * params.tint.b * k, 0.0f, 1.0f);
            Color& c = dst_row[x];
            // Screen: 1 - (1-a)(1-b), applied per channel.
            c.r = std::clamp(1.0f - (1.0f - c.r) * (1.0f - halo_r), 0.0f, 1.0f);
            c.g = std::clamp(1.0f - (1.0f - c.g) * (1.0f - halo_g), 0.0f, 1.0f);
            c.b = std::clamp(1.0f - (1.0f - c.b) * (1.0f - halo_b), 0.0f, 1.0f);
        }
    }
}

// ── Anamorphic streak ───────────────────────────────────────────────────────

void apply_anamorphic_streak(Framebuffer& fb, const AnamorphicStreakParams& params,
                             ::chronon3d::EffectScratchResources& scratch,
                             const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.threshold) || params.threshold < 0.0f || params.threshold > 1.0f ||
        !std::isfinite(params.softness) || params.softness < 0.0f || params.softness > 1.0f ||
        !std::isfinite(params.streak_length_px) || params.streak_length_px < 0.0f || params.streak_length_px > 512.0f ||
        !std::isfinite(params.intensity) || params.intensity < 0.0f || params.intensity > 8.0f ||
        fb.width() <= 0 || fb.height() <= 0)
        throw std::invalid_argument("anamorphic streak parameters or target surface are invalid");

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    scratch.ensure_size(w, h);
    Framebuffer& mask = *scratch.framebuffer;
    Framebuffer& blit_b = *scratch.framebuffer_b;
    mask.clear({0.0f, 0.0f, 0.0f, 0.0f});

    // 1. Highlight mask (same smooth knee as halation).
    const float knee = std::max(params.softness, 1.0e-3f);
    for (int y = 0; y < h; ++y) {
        const Color* src_row = fb.pixels_row(y);
        Color* dst_row = mask.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const Color& c = src_row[x];
            const float luma = kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
            const float t = std::clamp((luma - params.threshold) / knee, 0.0f, 1.0f);
            const float m = t * t * (3.0f - 2.0f * t);
            dst_row[x] = Color{m, m, m, m};
        }
    }

    // 2. Wide horizontal box blur only (1 px vertical): the classic anamorphic
    // streak is elongated purely along X.
    const int radius = static_cast<int>(std::lround(params.streak_length_px * 0.5f));
    if (radius > 0) {
        const int step = std::max(1, (2 * radius) / 32);
        for (int y = 0; y < h; ++y) {
            const Color* src_row = mask.pixels_row(y);
            Color* dst_row = blit_b.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                Color acc{0.0f, 0.0f, 0.0f, 0.0f};
                int used = 0;
                for (int tx = -radius; tx <= radius; tx += step) {
                    const int sx = std::clamp(x + tx, 0, w - 1);
                    acc.r += src_row[sx].r; acc.g += src_row[sx].g;
                    acc.b += src_row[sx].b; acc.a += src_row[sx].a;
                    ++used;
                }
                const float inv = 1.0f / static_cast<float>(std::max(used, 1));
                dst_row[x] = Color{acc.r * inv, acc.g * inv, acc.b * inv, acc.a * inv};
            }
        }
    }
    else {
        for (int y = 0; y < h; ++y) {
            const Color* src_row = mask.pixels_row(y);
            Color* dst_row = blit_b.pixels_row(y);
            for (int x = 0; x < w; ++x) dst_row[x] = src_row[x];
        }
    }

    // 3. Additive composite of the tinted streak.
    const float k = params.intensity;
    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const Color& m = blit_b.pixels_row(y)[x];
            if (m.r <= 0.0f && m.g <= 0.0f && m.b <= 0.0f) continue;
            const float r = m.r * params.tint.r * k;
            const float g = m.g * params.tint.g * k;
            const float b = m.b * params.tint.b * k;
            Color& c = dst_row[x];
            c.r = std::clamp(c.r + r, 0.0f, 1.0f);
            c.g = std::clamp(c.g + g, 0.0f, 1.0f);
            c.b = std::clamp(c.b + b, 0.0f, 1.0f);
        }
    }
}


// ── Continuous Film Grain & Archival Dust ───────────────────────────────────
void apply_film_grain_and_dust(Framebuffer& fb, const FilmGrainDustParams& params,
                               float time_seconds,
                               const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.intensity) || params.intensity <= 0.0f ||
        fb.width() <= 0 || fb.height() <= 0) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    const uint32_t timeQuantum = static_cast<uint32_t>(std::floor(time_seconds * 30.0f));
    const uint32_t baseSeed = params.seed ^ (timeQuantum * 0x9E3779B9u);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            // Deterministic splitmix pseudo-gaussian noise
            float n0 = hash_01(static_cast<float>(x), static_cast<float>(y), baseSeed);
            float n1 = hash_01(static_cast<float>(x + 31), static_cast<float>(y + 17), baseSeed + 13u);
            float n2 = hash_01(static_cast<float>(x + 89), static_cast<float>(y + 53), baseSeed + 97u);
            float noise = ((n0 + n1 + n2) / 3.0f - 0.5f) * params.intensity * 2.0f;

            // Preserve alpha, modulate RGB
            c.r = std::clamp(c.r + noise, 0.0f, 1.0f);
            c.g = std::clamp(c.g + noise, 0.0f, 1.0f);
            c.b = std::clamp(c.b + noise, 0.0f, 1.0f);
            fb.set_pixel(x, y, c);
        }
    }

    // Dynamic archival dust hairs/scratches
    if (params.dust_density > 0.0f) {
        const int dustCount = static_cast<int>((x1 - x0) * (y1 - y0) * 0.00003f * params.dust_density);
        uint32_t dSeed = baseSeed + 4242u;
        for (int i = 0; i < dustCount; ++i) {
            dSeed = dSeed * 1664525u + 1013904223u;
            int sx = x0 + static_cast<int>(dSeed % (x1 - x0));
            dSeed = dSeed * 1664525u + 1013904223u;
            int sy = y0 + static_cast<int>(dSeed % (y1 - y0));
            dSeed = dSeed * 1664525u + 1013904223u;
            float bright = 0.55f + 0.35f * static_cast<float>(dSeed & 0xFF) / 255.0f;

            if ((dSeed & 1) != 0) {
                // Short hair scratch (2-4 pixels)
                dSeed = dSeed * 1664525u + 1013904223u;
                int len = 2 + (dSeed % 3);
                for (int l = 0; l < len; ++l) {
                    int px = std::clamp(sx + l, 0, w - 1);
                    int py = std::clamp(sy + (l % 2), 0, h - 1);
                    Color cur = fb.get_pixel(px, py);
                    cur.r = std::min(1.0f, cur.r + bright * 0.4f);
                    cur.g = std::min(1.0f, cur.g + bright * 0.4f);
                    cur.b = std::min(1.0f, cur.b + bright * 0.4f);
                    fb.set_pixel(px, py, cur);
                }
            } else {
                Color cur = fb.get_pixel(sx, sy);
                cur.r = std::min(1.0f, cur.r + bright * 0.5f);
                cur.g = std::min(1.0f, cur.g + bright * 0.5f);
                cur.b = std::min(1.0f, cur.b + bright * 0.5f);
                fb.set_pixel(sx, sy, cur);
            }
        }
    }
}

// ── Multi-pass Optical Bloom ────────────────────────────────────────────────
void apply_optical_bloom(Framebuffer& fb, const BloomParams& params,
                         ::chronon3d::EffectScratchResources& scratch,
                         const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.threshold) || !std::isfinite(params.radius) || !std::isfinite(params.intensity) ||
        params.radius <= 0.0f || params.intensity <= 0.0f ||
        fb.width() <= 0 || fb.height() <= 0) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    scratch.ensure_size(w, h);
    Framebuffer& mask = *scratch.framebuffer;
    Framebuffer& blur_h = *scratch.framebuffer_b;
    mask.clear({0.0f, 0.0f, 0.0f, 0.0f});

    // 1. Highlight extraction with smooth knee
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            float luma = kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
            if (luma > params.threshold) {
                float ramp = (luma - params.threshold) / std::max(0.001f, 1.0f - params.threshold);
                float weight = ramp * ramp * (3.0f - 2.0f * ramp);
                mask.set_pixel(x, y, {c.r * weight, c.g * weight, c.b * weight, c.a * weight});
            }
        }
    }

    // 2. Separable box blur approximation of Gaussian bloom
    const int radius = std::clamp(static_cast<int>(std::round(params.radius)), 1, 64);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            blur_h.set_pixel(x, y, horizontal_average(mask, static_cast<float>(x), y, radius, 11));
        }
    }

    // 3. Vertical pass and additive warm optical halation composite
    const Color warmTint{1.04f, 0.98f, 0.92f, 1.0f};
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color acc{0.0f, 0.0f, 0.0f, 0.0f};
            int count = 0;
            const int step = std::max(1, radius / 5);
            for (int dy = -radius; dy <= radius; dy += step) {
                int sy = std::clamp(y + dy, 0, h - 1);
                Color s = blur_h.get_pixel(x, sy);
                acc.r += s.r; acc.g += s.g; acc.b += s.b;
                count++;
            }
            if (count > 0) {
                float inv = 1.0f / static_cast<float>(count);
                Color base = fb.get_pixel(x, y);
                base.r = std::clamp(base.r + acc.r * inv * params.intensity * warmTint.r, 0.0f, 1.0f);
                base.g = std::clamp(base.g + acc.g * inv * params.intensity * warmTint.g, 0.0f, 1.0f);
                base.b = std::clamp(base.b + acc.b * inv * params.intensity * warmTint.b, 0.0f, 1.0f);
                fb.set_pixel(x, y, base);
            }
        }
    }
}

} // namespace chronon3d::renderer
