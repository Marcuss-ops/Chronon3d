#pragma once

// ---------------------------------------------------------------------------
// camera_ring.hpp — shared analytic contract for the Camera Ring / Petzval
// field-curvature defocus effect.
//
// The CPU kernel, the Vulkan shader and the diagnostic tests all consume the
// same normalized-space model:
//
//   delta = (uv - center) / axis_scale
//   r     = length(delta)
//   CoC   = smoothstep(inner_radius, outer_radius, r)
//
// `axis_scale` is the elliptical eccentricity: a smaller Y coefficient keeps
// a horizontal text corridor sharp while the top/bottom edges fall outside
// the Petzval focal surface. The blur radius is `CoC * max_blur_radius` in
// UV-width units; the Y offset is multiplied by aspect ratio so the bokeh
// disc stays circular in pixel space. Chromatic dispersion is proportional to
// CoC² and therefore exactly zero on the optical axis.
// ---------------------------------------------------------------------------

#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace chronon3d::effects {

// 16-sample Poisson disc from the production optical shader contract.
inline constexpr std::array<Vec2, 16> kCameraRingPoissonDisc{{
    Vec2{-0.326212f, -0.405810f}, Vec2{-0.840144f, -0.073580f},
    Vec2{-0.695914f,  0.457137f}, Vec2{-0.203345f,  0.620716f},
    Vec2{ 0.962340f, -0.194983f}, Vec2{ 0.473434f, -0.480026f},
    Vec2{ 0.519456f,  0.767022f}, Vec2{ 0.185461f, -0.893124f},
    Vec2{ 0.507431f,  0.064425f}, Vec2{ 0.896420f,  0.412458f},
    Vec2{-0.321940f, -0.932615f}, Vec2{-0.791559f, -0.597710f},
    Vec2{-0.000100f, -0.100000f}, Vec2{-0.370000f,  0.180000f},
    Vec2{ 0.220000f,  0.340000f}, Vec2{-0.110000f, -0.280000f},
}};

/// Normalized circle-of-confusion map in [0, 1]. `u` and `v` are pixel-centre
/// normalized screen coordinates ([0, 1] across the full frame). The function
/// is C1-continuous across both the inner safe-zone boundary and the outer
/// saturation radius because smoothstep has zero derivative at both ends.
[[nodiscard]] inline float camera_ring_coc(
    const CameraRingParams& params, float u, float v) noexcept {
    const float sx = std::max(params.axis_scale.x, 1.0e-4f);
    const float sy = std::max(params.axis_scale.y, 1.0e-4f);
    const float dx = (u - params.center.x) / sx;
    const float dy = (v - params.center.y) / sy;
    const float radius = std::sqrt(dx * dx + dy * dy);
    const float span = std::max(params.outer_radius - params.inner_radius, 1.0e-6f);
    const float t = std::clamp((radius - params.inner_radius) / span, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Dynamic rig coupling from the reference camera stress test: at rest the
/// authored safe zone and blur radius are used; at 1200 scene units/second
/// the safe zone narrows to 50% and the maximum defocus grows to 28/12 of the
/// authored radius. Values above 1200 clamp to the fully-coupled state.
struct CameraRingRigCoupling {
    float inner_radius{0.0f};
    float max_blur_radius{0.0f};
};

[[nodiscard]] inline CameraRingRigCoupling camera_ring_coupled_optics(
    const CameraRingParams& params, float camera_speed_units_per_second) noexcept {
    const float t = std::clamp(camera_speed_units_per_second / 1200.0f, 0.0f, 1.0f);
    CameraRingRigCoupling coupled;
    coupled.inner_radius = params.inner_radius * (1.0f + (0.5f - 1.0f) * t);
    coupled.max_blur_radius =
        params.max_blur_radius * (1.0f + (28.0f / 12.0f - 1.0f) * t);
    return coupled;
}

}  // namespace chronon3d::effects
