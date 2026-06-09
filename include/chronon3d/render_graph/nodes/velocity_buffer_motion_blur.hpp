#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace chronon3d {

// ── VelocityBufferMotionBlur ────────────────────────────────────────────────
//
// Post-process motion blur that uses an *estimated* per-pixel velocity field
// derived from block-matching between the current and previous frame.
//
// In a GPU pipeline the velocity would be written into a dedicated G-buffer
// (R = dx, G = dy) during the geometry pass. Without geometry access, this
// implementation does a small search-window block-match against the previous
// frame, which gives a usable screen-space velocity approximation for
// cinematic motion blur. Static regions return zero velocity and pass through.
//
// Algorithm:
//   1. For each NxN tile in the current frame, search a (2*radius+1)^2
//      window in the previous frame. The best-matching offset is the
//      velocity vector for that tile (current → previous).
//   2. For each pixel, sample the current frame along the negative
//      velocity direction N times and accumulate the result. This produces
//      a directional smear whose length matches the per-pixel velocity.
//
// Usage:
//   VelocityBufferMotionBlur mb;
//   mb.set_block_size(8);
//   mb.set_search_radius(16);
//   mb.set_samples(8);
//   mb.set_strength(1.0f);
//   Framebuffer& current = ...;
//   Framebuffer& output  = ...;
//   mb.apply(current, output);   // also stores `current` as prev for next call
//
// The class owns the previous frame buffer internally. Call reset() to clear
// the history (e.g. on scene change).

class VelocityBufferMotionBlur {
public:
    struct Settings {
        i32  block_size{8};          // NxN tile size for block matching
        i32  search_radius{16};      // ±radius pixel search window
        i32  samples{8};             // taps along the velocity direction
        f32  strength{1.0f};         // 0 = no blur, 1 = full velocity
        f32  motion_threshold{0.02f};// luminance diff to consider a pixel moving
        f32  max_velocity{64.0f};    // clamp velocity magnitude (px/frame)
    };

    VelocityBufferMotionBlur() = default;

    VelocityBufferMotionBlur& set_block_size(i32 v)        { m_settings.block_size = std::max(2, v); return *this; }
    VelocityBufferMotionBlur& set_search_radius(i32 v)     { m_settings.search_radius = std::max(1, v); return *this; }
    VelocityBufferMotionBlur& set_samples(i32 v)           { m_settings.samples = std::max(2, v); return *this; }
    VelocityBufferMotionBlur& set_strength(f32 v)          { m_settings.strength = std::clamp(v, 0.0f, 4.0f); return *this; }
    VelocityBufferMotionBlur& set_motion_threshold(f32 v)  { m_settings.motion_threshold = std::max(0.0f, v); return *this; }
    VelocityBufferMotionBlur& set_max_velocity(f32 v)      { m_settings.max_velocity = std::max(1.0f, v); return *this; }

    [[nodiscard]] const Settings& settings() const { return m_settings; }
    Settings& settings() { return m_settings; }

    // Returns true if there is a previous frame stored (i.e. apply can produce
    // a velocity-based blur rather than a pass-through).
    [[nodiscard]] bool has_history() const {
        return m_prev &&
               m_prev->width()  > 0 &&
               m_prev->height() > 0 &&
               m_prev->width()  == m_last_width &&
               m_prev->height() == m_last_height;
    }

    // Clear stored history. The next apply() call will be a pass-through.
    void reset() {
        m_prev.reset();
        m_last_width = 0;
        m_last_height = 0;
    }

    // Apply motion blur to `current`, writing into `output`. After the call,
    // a copy of `current` is stored as the previous frame for next time.
    //
    // The `output` framebuffer must be the same size as `current`. The
    // `current` framebuffer is left untouched.
    void apply(const Framebuffer& current, Framebuffer& output) {
        const i32 W = current.width();
        const i32 H = current.height();
        if (W <= 0 || H <= 0) return;
        if (output.width() != W || output.height() != H) return;

        // First call: just copy and store history.
        if (!has_history()) {
            output.clear(Color{0, 0, 0, 0});
            for (i32 y = 0; y < H; ++y) {
                const Color* src_row = current.pixels_row(y);
                Color*       dst_row = output.pixels_row(y);
                for (i32 x = 0; x < W; ++x) dst_row[x] = src_row[x];
            }
            store_history(current);
            return;
        }

        // 1) Compute a per-tile velocity field via block matching.
        compute_velocity_field(current);

        // 2) For each pixel, sample along the (negative) velocity direction.
        apply_directional_blur(current, output);

        // 3) Persist current as the new previous frame.
        store_history(current);
    }

    // Apply motion blur to `current` in-place (output == current).
    // Done by copying current into a scratch buffer, then writing back.
    // Slightly more expensive than apply(current, other) but convenient.
    void apply_inplace(Framebuffer& current) {
        Framebuffer scratch(current.width(), current.height());
        scratch.clear(Color{0, 0, 0, 0});
        apply(current, scratch);
        // Copy scratch back into current
        for (i32 y = 0; y < current.height(); ++y) {
            const Color* src = scratch.pixels_row(y);
            Color*       dst = current.pixels_row(y);
            for (i32 x = 0; x < current.width(); ++x) dst[x] = src[x];
        }
    }

    // Access the previous frame for inspection / testing.
    [[nodiscard]] const Framebuffer* previous_frame() const { return m_prev.get(); }

private:
    // Resize / refresh the stored previous frame.
    void store_history(const Framebuffer& current) {
        if (!m_prev ||
            m_prev->width()  != current.width() ||
            m_prev->height() != current.height()) {
            m_prev = std::make_unique<Framebuffer>(current.width(), current.height());
        }
        const i32 W = current.width();
        const i32 H = current.height();
        for (i32 y = 0; y < H; ++y) {
            const Color* src = current.pixels_row(y);
            Color*       dst = m_prev->pixels_row(y);
            for (i32 x = 0; x < W; ++x) dst[x] = src[x];
        }
        m_last_width = W;
        m_last_height = H;
    }

    // SAD (sum of absolute differences) between two blocks of size N.
    [[nodiscard]] static f32 block_sad(const Framebuffer& a, const Framebuffer& b,
                                        i32 ax, i32 ay, i32 bx, i32 by, i32 n) {
        f32 sad = 0.0f;
        for (i32 dy = 0; dy < n; ++dy) {
            const Color* ra = a.pixels_row(ay + dy);
            const Color* rb = b.pixels_row(by + dy);
            for (i32 dx = 0; dx < n; ++dx) {
                const Color& ca = ra[ax + dx];
                const Color& cb = rb[bx + dx];
                sad += std::abs(ca.r - cb.r) + std::abs(ca.g - cb.g) + std::abs(ca.b - cb.b);
            }
        }
        return sad;
    }

    // Compute the velocity field by block matching current against m_prev.
    void compute_velocity_field(const Framebuffer& current) {
        const i32 W = current.width();
        const i32 H = current.height();
        const i32 N = m_settings.block_size;
        const i32 R = m_settings.search_radius;

        const i32 tiles_x = (W + N - 1) / N;
        const i32 tiles_y = (H + N - 1) / N;

        if (static_cast<i32>(m_velocities.size()) != tiles_x * tiles_y) {
            m_velocities.assign(tiles_x * tiles_y, Vec2{0.0f, 0.0f});
        }

        for (i32 ty = 0; ty < tiles_y; ++ty) {
            for (i32 tx = 0; tx < tiles_x; ++tx) {
                const i32 ax = tx * N;
                const i32 ay = ty * N;
                const i32 n_eff_x = std::min(N, W - ax);
                const i32 n_eff_y = std::min(N, H - ay);
                if (n_eff_x <= 0 || n_eff_y <= 0) continue;

                f32 best_sad = std::numeric_limits<f32>::infinity();
                i32 best_dx = 0;
                i32 best_dy = 0;

                // Helper: update best if `sad` is strictly better, or equal in
                // SAD but with a smaller |offset| (preferring 0,0 ties). This
                // makes the matcher return zero velocity for perfectly static
                // frames, where every candidate has SAD=0.
                auto consider = [&](i32 ox, i32 oy, f32 sad) {
                    const f32 new_len = static_cast<f32>(ox * ox + oy * oy);
                    const f32 best_len = static_cast<f32>(best_dx * best_dx + best_dy * best_dy);
                    if (sad < best_sad || (sad == best_sad && new_len < best_len)) {
                        best_sad = sad;
                        best_dx = ox;
                        best_dy = oy;
                    }
                };

                // Search (2R+1)^2 candidate offsets.
                for (i32 oy = -R; oy <= R; oy += 2) {
                    for (i32 ox = -R; ox <= R; ox += 2) {
                        const i32 bx = ax + ox;
                        const i32 by = ay + oy;
                        if (bx < 0 || by < 0 || bx + N > W || by + N > H) continue;
                        const f32 sad = block_sad(current, *m_prev, ax, ay, bx, by, N);
                        consider(ox, oy, sad);
                    }
                }

                // Refine on a 3x3 neighborhood around the best offset.
                for (i32 oy = best_dy - 1; oy <= best_dy + 1; ++oy) {
                    for (i32 ox = best_dx - 1; ox <= best_dx + 1; ++ox) {
                        const i32 bx = ax + ox;
                        const i32 by = ay + oy;
                        if (bx < 0 || by < 0 || bx + N > W || by + N > H) continue;
                        const f32 sad = block_sad(current, *m_prev, ax, ay, bx, by, N);
                        consider(ox, oy, sad);
                    }
                }

                Vec2 v{static_cast<f32>(best_dx), static_cast<f32>(best_dy)};

                // Reject small-magnitude "matches" that are just noise.
                if (glm::length(v) < 0.5f) v = Vec2{0.0f, 0.0f};

                // Clamp to max_velocity.
                const f32 len = glm::length(v);
                if (len > m_settings.max_velocity) {
                    v *= (m_settings.max_velocity / len);
                }

                m_velocities[ty * tiles_x + tx] = v;
            }
        }

        m_tiles_x = tiles_x;
        m_tiles_y = tiles_y;
    }

    // Sample the velocity at pixel (x, y) by bilinear interpolation of the
    // tile-based velocity field.
    [[nodiscard]] Vec2 sample_velocity(i32 x, i32 y) const {
        if (m_velocities.empty() || m_tiles_x == 0 || m_tiles_y == 0) {
            return Vec2{0.0f, 0.0f};
        }
        const f32 N = static_cast<f32>(m_settings.block_size);
        const f32 fx = (static_cast<f32>(x) + 0.5f) / N - 0.5f;
        const f32 fy = (static_cast<f32>(y) + 0.5f) / N - 0.5f;
        const f32 cx = std::clamp(fx, 0.0f, static_cast<f32>(m_tiles_x - 1));
        const f32 cy = std::clamp(fy, 0.0f, static_cast<f32>(m_tiles_y - 1));

        const i32 x0 = static_cast<i32>(std::floor(cx));
        const i32 y0 = static_cast<i32>(std::floor(cy));
        const i32 x1 = std::min(x0 + 1, m_tiles_x - 1);
        const i32 y1 = std::min(y0 + 1, m_tiles_y - 1);
        const f32 tx = cx - static_cast<f32>(x0);
        const f32 ty = cy - static_cast<f32>(y0);

        const Vec2 v00 = m_velocities[y0 * m_tiles_x + x0];
        const Vec2 v10 = m_velocities[y0 * m_tiles_x + x1];
        const Vec2 v01 = m_velocities[y1 * m_tiles_x + x0];
        const Vec2 v11 = m_velocities[y1 * m_tiles_x + x1];

        const Vec2 vx0 = v00 * (1.0f - tx) + v10 * tx;
        const Vec2 vx1 = v01 * (1.0f - tx) + v11 * tx;
        return vx0 * (1.0f - ty) + vx1 * ty;
    }

    // For each pixel, sample the current frame N times along the negative
    // velocity direction and accumulate the average.
    void apply_directional_blur(const Framebuffer& current, Framebuffer& output) {
        const i32 W = current.width();
        const i32 H = current.height();
        const i32 N = std::max(2, m_settings.samples);

        for (i32 y = 0; y < H; ++y) {
            Color* dst_row = output.pixels_row(y);
            for (i32 x = 0; x < W; ++x) {
                Vec2 v = sample_velocity(x, y) * m_settings.strength;

                // No motion: pass through the current pixel.
                if (glm::length(v) < 0.01f) {
                    dst_row[x] = current.get_pixel(x, y);
                    continue;
                }

                // Sample along the velocity direction. We walk the line
                // from the current pixel back toward the previous position
                // (i.e. -v) and average the taps.
                f32 r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                for (i32 s = 0; s < N; ++s) {
                    const f32 u = (static_cast<f32>(s) + 0.5f) / static_cast<f32>(N);
                    const f32 sx = static_cast<f32>(x) - v.x * u;
                    const f32 sy = static_cast<f32>(y) - v.y * u;
                    const Color c = current.sample_bilinear(sx, sy);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                }
                const f32 inv = 1.0f / static_cast<f32>(N);
                dst_row[x] = Color{r * inv, g * inv, b * inv, a * inv};
            }
        }
    }

    Settings            m_settings{};
    std::unique_ptr<Framebuffer> m_prev;
    i32                 m_last_width{0};
    i32                 m_last_height{0};

    // Velocity field: one Vec2 (dx, dy) per NxN tile in the current frame.
    std::vector<Vec2>   m_velocities;
    i32                 m_tiles_x{0};
    i32                 m_tiles_y{0};
};

} // namespace chronon3d
