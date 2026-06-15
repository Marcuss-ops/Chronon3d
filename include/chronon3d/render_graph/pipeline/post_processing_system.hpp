#pragma once

#include <chronon3d/render_graph/nodes/velocity_buffer_motion_blur.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace chronon3d {

// ── PostProcessingSystem ─────────────────────────────────────────────────────
//
// Facade class that bundles the cinematic post-processing passes into a single
// configurable system. Sits at the end of the rendering pipeline and is fed
// the composed framebuffer. Internally it owns:
//   - a per-pixel depth-of-field blur (via the existing DofEffectNode logic)
//   - a velocity-buffer motion blur (VelocityBufferMotionBlur)
//
// Usage:
//   PostProcessingSystem pp;
//   pp.enable_depth_of_field(true);
//   pp.enable_motion_blur(true);
//   pp.set_dof({.focus_z = 0, .aperture = 0.02f, .max_blur = 16});
//   pp.set_motion_blur_samples(8);
//   pp.set_motion_blur_strength(1.0f);
//
//   // Per frame:
//   pp.apply(composed_framebuffer, output_framebuffer, camera_2_5d, layer_z);
//
// The class is deterministic and stateful (it stores the previous frame for
// motion estimation). Call reset() between unrelated renders.

class PostProcessingSystem {
public:
    PostProcessingSystem() = default;

    // ── Enable / disable passes ──────────────────────────────────────────────

    PostProcessingSystem& enable_depth_of_field(bool v) { m_dof_enabled = v; return *this; }
    PostProcessingSystem& enable_motion_blur(bool v)    { m_motion_blur_enabled = v; return *this; }

    [[nodiscard]] bool depth_of_field_enabled() const { return m_dof_enabled; }
    [[nodiscard]] bool motion_blur_enabled()    const { return m_motion_blur_enabled; }

    // ── Configuration ───────────────────────────────────────────────────────

    PostProcessingSystem& set_dof(DepthOfFieldSettings v)   { m_dof = v; return *this; }
    [[nodiscard]] const DepthOfFieldSettings& dof() const   { return m_dof; }

    PostProcessingSystem& set_motion_blur_block_size(i32 v)     { m_motion_blur.set_block_size(v); return *this; }
    PostProcessingSystem& set_motion_blur_search_radius(i32 v)  { m_motion_blur.set_search_radius(v); return *this; }
    PostProcessingSystem& set_motion_blur_samples(i32 v)        { m_motion_blur.set_samples(v); return *this; }
    PostProcessingSystem& set_motion_blur_strength(f32 v)       { m_motion_blur.set_strength(v); return *this; }
    PostProcessingSystem& set_motion_blur_max_velocity(f32 v)   { m_motion_blur.set_max_velocity(v); return *this; }

    [[nodiscard]] const VelocityBufferMotionBlur& motion_blur() const { return m_motion_blur; }
    VelocityBufferMotionBlur& motion_blur() { return m_motion_blur; }

    // ── Per-frame apply ─────────────────────────────────────────────────────

    // Apply the active post-processing passes to `input`, writing into `output`.
    //
    // `output` must be the same size as `input`. `input` is not modified.
    // The camera and layer Z are used by the DOF pass to compute the
    // per-pixel blur radius.
    //
    // Returns the framebuffer that contains the final result (== output).
    Framebuffer& apply(const Framebuffer& input,
                       Framebuffer& output,
                       const Camera2_5D& camera = {},
                       f32 layer_world_z = 0.0f) {
        const i32 W = input.width();
        const i32 H = input.height();
        if (W <= 0 || H <= 0) return output;
        if (output.width() != W || output.height() != H) return output;

        // Step 1: copy input → scratch_a (so we can chain passes).
        if (!m_scratch_a || m_scratch_a->width() != W || m_scratch_a->height() != H) {
            m_scratch_a = std::make_unique<Framebuffer>(W, H);
        }
        copy(input, *m_scratch_a);

        // Step 2: Depth of Field — read scratch_a, write scratch_b.
        if (m_dof_enabled && m_dof.enabled) {
            if (!m_scratch_b || m_scratch_b->width() != W || m_scratch_b->height() != H) {
                m_scratch_b = std::make_unique<Framebuffer>(W, H);
            }
            apply_dof(*m_scratch_a, *m_scratch_b, camera, layer_world_z);
            std::swap(m_scratch_a, m_scratch_b);
        }

        // Step 3: Motion Blur (velocity buffer) — read scratch_a, write output.
        if (m_motion_blur_enabled) {
            m_motion_blur.apply(*m_scratch_a, output);
        } else {
            copy(*m_scratch_a, output);
        }

        return output;
    }

    // Reset all internal state (motion-blur history, scratch buffers).
    void reset() {
        m_motion_blur.reset();
        m_scratch_a.reset();
        m_scratch_b.reset();
    }

private:
    static void copy(const Framebuffer& src, Framebuffer& dst) {
        const i32 H = src.height();
        for (i32 y = 0; y < H; ++y) {
            const Color* sr = src.pixels_row(y);
            Color*       dr = dst.pixels_row(y);
            for (i32 x = 0; x < src.width(); ++x) dr[x] = sr[x];
        }
    }

    // Compute a DOF blur radius for the layer (single-pass, no per-pixel depth
    // because CPU doesn't have a depth buffer; uses the layer's world Z as
    // its depth). This delegates to compute_dof_blur_radius() which supports
    // both the legacy linear model and the physical thin-lens CoC model.
    [[nodiscard]] f32 dof_blur_radius(const Camera2_5D& camera, f32 layer_world_z, f32 viewport_w) const {
        if (!m_dof.enabled) return 0.0f;
        return compute_dof_blur_radius(m_dof, camera.lens, layer_world_z, viewport_w);
    }

    // Apply a separable box-blur with radius `r` to `src` → `dst`. Used as
    // a CPU-friendly approximation of a Gaussian for DOF.
    void apply_dof(const Framebuffer& src, Framebuffer& dst,
                   const Camera2_5D& camera, f32 layer_world_z) {
        const f32 r = dof_blur_radius(camera, layer_world_z, static_cast<f32>(src.width()));
        const i32 W = src.width();
        const i32 H = src.height();

        if (r < 0.5f) {
            copy(src, dst);
            return;
        }

        const i32 radius = static_cast<i32>(std::round(r));
        const i32 diameter = 2 * radius + 1;

        // Horizontal pass: src → tmp
        if (!m_blur_tmp || m_blur_tmp->width() != W || m_blur_tmp->height() != H) {
            m_blur_tmp = std::make_unique<Framebuffer>(W, H);
        }

        for (i32 y = 0; y < H; ++y) {
            const Color* src_row = src.pixels_row(y);
            Color*       tmp_row = m_blur_tmp->pixels_row(y);
            for (i32 x = 0; x < W; ++x) {
                f32 rr = 0.0f, gg = 0.0f, bb = 0.0f, aa = 0.0f;
                i32 count = 0;
                for (i32 kx = -radius; kx <= radius; ++kx) {
                    const i32 xx = std::clamp(x + kx, 0, W - 1);
                    const Color& c = src_row[xx];
                    rr += c.r; gg += c.g; bb += c.b; aa += c.a;
                    ++count;
                }
                const f32 inv = 1.0f / static_cast<f32>(count);
                tmp_row[x] = Color{rr * inv, gg * inv, bb * inv, aa * inv};
            }
        }

        // Vertical pass: tmp → dst
        for (i32 y = 0; y < H; ++y) {
            Color* dst_row = dst.pixels_row(y);
            for (i32 x = 0; x < W; ++x) {
                f32 rr = 0.0f, gg = 0.0f, bb = 0.0f, aa = 0.0f;
                i32 count = 0;
                for (i32 ky = -radius; ky <= radius; ++ky) {
                    const i32 yy = std::clamp(y + ky, 0, H - 1);
                    const Color& c = m_blur_tmp->get_pixel(x, yy);
                    rr += c.r; gg += c.g; bb += c.b; aa += c.a;
                    ++count;
                }
                const f32 inv = 1.0f / static_cast<f32>(count);
                dst_row[x] = Color{rr * inv, gg * inv, bb * inv, aa * inv};
            }
        }
    }

    // ── State ───────────────────────────────────────────────────────────────
    bool m_dof_enabled{true};
    bool m_motion_blur_enabled{false};
    DepthOfFieldSettings m_dof{};

    VelocityBufferMotionBlur m_motion_blur;

    std::unique_ptr<Framebuffer> m_scratch_a;
    std::unique_ptr<Framebuffer> m_scratch_b;
    std::unique_ptr<Framebuffer> m_blur_tmp;
};

} // namespace chronon3d
