#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::rendering {

// Depth-based grading — applies fog, saturation, contrast, and blur
// adjustments based on a layer's Z distance from the camera.
//
// This is *art direction* for depth, not a physics-based fog effect.
// It lets you make far objects desaturated, lower-contrast, and slightly
// blurred to push them into the background, giving the scene a strong
// sense of depth even with an orthographic or weak-perspective camera.
//
// Usage:
//   DepthGrade grade;
//   grade.near_z = 300.0f;
//   grade.far_z  = 1800.0f;
//
//   // For a pixel at camera-space depth z:
//   float t = grade.normalized_depth(z);        // 0=near, 1=far
//   Color graded = grade.apply(base_color, t);  // fog + desat + contrast
//   float blur = grade.blur_for_depth(t);       // depth-of-field hint
//
struct DepthGrade {
    bool  enabled{true};

    // ── Depth range (camera-space Z, same convention as DepthRole) ──
    // Convention: Z positive = farther from camera.
    f32   near_z{300.0f};
    f32   far_z{1800.0f};

    // ── Fog ───────────────────────────────────────────────────────
    Color fog_color{0.02f, 0.02f, 0.06f, 1.0f};
    f32   fog_opacity{0.35f};

    // ── Saturation / Contrast ─────────────────────────────────────
    // At far_z, saturation is multiplied by far_saturation and
    // contrast by far_contrast.  Values <1 desaturate/flatten.
    f32   far_saturation{0.65f};
    f32   far_contrast{0.75f};

    // ── Depth blur hint ───────────────────────────────────────────
    // Maximum blur radius applied at far_z.  The renderer uses this
    // as a hint; actual application depends on the render backend.
    f32   far_blur_px{4.0f};

    // ── Smoothstep helper ─────────────────────────────────────────
    [[nodiscard]] static f32 smoothstep(f32 edge0, f32 edge1, f32 x) noexcept {
        const f32 t = std::clamp((x - edge0) / (edge1 - edge0 + 1e-8f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Returns the normalized depth factor [0..1] for a given camera-space Z.
    // 0 = at near_z (or closer), 1 = at far_z (or farther).
    [[nodiscard]] f32 normalized_depth(f32 cam_z) const noexcept {
        return smoothstep(near_z, far_z, cam_z);
    }

    // Returns the blur radius hint for a given normalized depth factor.
    [[nodiscard]] f32 blur_for_depth(f32 t) const noexcept {
        return t * far_blur_px;
    }

    // Applies fog, saturation, and contrast grading to a color.
    // t = normalized_depth result for the pixel's Z position.
    [[nodiscard]] Color apply(Color base, f32 t) const noexcept {
        if (!enabled || t <= 0.0f) return base;

        // ── Fog mix ──
        Color c{
            base.r + (fog_color.r - base.r) * t * fog_opacity,
            base.g + (fog_color.g - base.g) * t * fog_opacity,
            base.b + (fog_color.b - base.b) * t * fog_opacity,
            base.a
        };

        // ── Saturation (luminance-preserving desaturation) ──
        const f32 sat = 1.0f + t * (far_saturation - 1.0f); // lerps from 1.0 → far_saturation
        if (sat < 1.0f) {
            // Rec. 709 luminance weights
            const f32 luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            c.r = luma + sat * (c.r - luma);
            c.g = luma + sat * (c.g - luma);
            c.b = luma + sat * (c.b - luma);
        }

        // ── Contrast ──
        const f32 contrast = 1.0f + t * (far_contrast - 1.0f); // lerps from 1.0 → far_contrast
        if (contrast < 1.0f) {
            c.r = 0.5f + contrast * (c.r - 0.5f);
            c.g = 0.5f + contrast * (c.g - 0.5f);
            c.b = 0.5f + contrast * (c.b - 0.5f);
        }

        return c;
    }

    // ── Presets ──────────────────────────────────────────────────

    /// Atmospheric haze — subtle desaturation and fog for outdoor scenes.
    static DepthGrade atmospheric() {
        DepthGrade g;
        g.near_z          = 200.0f;
        g.far_z           = 1600.0f;
        g.fog_color       = {0.06f, 0.07f, 0.12f, 1.0f};
        g.fog_opacity     = 0.25f;
        g.far_saturation  = 0.70f;
        g.far_contrast    = 0.80f;
        g.far_blur_px     = 3.0f;
        return g;
    }

    /// Deep depth — strong depth separation for dramatic reveals.
    static DepthGrade deep_depth() {
        DepthGrade g;
        g.near_z          = 300.0f;
        g.far_z           = 2000.0f;
        g.fog_color       = {0.01f, 0.01f, 0.04f, 1.0f};
        g.fog_opacity     = 0.45f;
        g.far_saturation  = 0.50f;
        g.far_contrast    = 0.65f;
        g.far_blur_px     = 6.0f;
        return g;
    }

    /// Subtle — barely-there grading, good for clean product shots.
    static DepthGrade subtle() {
        DepthGrade g;
        g.near_z          = 500.0f;
        g.far_z           = 1200.0f;
        g.fog_color       = {0.03f, 0.03f, 0.05f, 1.0f};
        g.fog_opacity     = 0.15f;
        g.far_saturation  = 0.85f;
        g.far_contrast    = 0.90f;
        g.far_blur_px     = 2.0f;
        return g;
    }
};

} // namespace chronon3d::rendering
