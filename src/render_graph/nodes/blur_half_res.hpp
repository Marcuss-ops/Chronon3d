// blur_half_res.hpp — the ONE decision for the native full-frame blur's
// half-resolution fast path.
//
// Why this exists: the measured production clips spend 62–77% of their summed
// GPU pass time in full-frame Gaussian blur (up to 40 separable blur passes per
// frame, 19–53 ms/frame against a 41.7 ms budget at 24 fps). A blur is a
// low-pass filter: computing it on a half-resolution plate and upscaling the
// result is visually equivalent for radii where the kernel already discards the
// high frequencies, and it removes 75% of the pixels each pass must touch.
//
// The decision is deliberately pure so it can be pinned without a GPU. The
// caller (try_native_full_frame_blur) owns surface allocation and dispatch; this
// header owns only "is half-res allowed for this effect?".
//
// Safety rules:
//   - opt-out env CHRONON3D_BLUR_HALF_RES=0|false|off keeps the historical
//     full-resolution path byte-identical;
//   - a partial clip or a non-zero surface origin takes the full-resolution
//     path: the affine downscale/upscale would have to re-derive a clipped
//     source rectangle, and a wrong halo is worse than a slower blur;
//   - small canvases and small radii take the full-resolution path: below
//     ~8 px the half-res resample is visible and the saved work is noise.
#pragma once

#include <cstdlib>
#include <string_view>

namespace chronon3d::graph::detail {

// blur_half_res_env_enabled answers the opt-out switch. Default is ON: the
// measured production corpus is radius 24 (clip background) and the A/B gate
// lives in docs/tickets, not in every deployment's environment.
inline bool blur_half_res_env_enabled() {
    const char* value = std::getenv("CHRONON3D_BLUR_HALF_RES");
    if (value == nullptr) return true;
    const std::string_view setting(value);
    return !(setting == "0" || setting == "false" || setting == "off");
}

// blur_half_res_eligible is the complete admission rule. It takes the already
// resolved flag so tests can exercise every branch without touching the
// process environment.
inline bool blur_half_res_eligible(int width, int height, float radius,
                                   bool partial_clip, bool origin_zero,
                                   bool flag_on) {
    if (!flag_on) return false;
    if (partial_clip || !origin_zero) return false;
    if (width < 320 || height < 240) return false;
    // NaN-safe: a NaN radius must never be promoted into the fast path.
    if (!(radius >= 8.0f)) return false;
    return true;
}

}  // namespace chronon3d::graph::detail
