#pragma once

#include "../../commands.hpp"

#include <string_view>

namespace chronon3d::cli {

/// Apply curated defaults to the existing RenderArgs surface. `is_explicit`
/// reports whether a low-level CLI option was supplied by the user; explicit
/// values always win. This is not a second renderer configuration.
///
/// Profiles may tune quality/performance only. They must never enable
/// diagnostic overlays or other output-altering inspection features.
template <typename IsExplicit>
void apply_render_profile(RenderArgs& args,
                          std::string_view profile,
                          IsExplicit&& is_explicit) {
    if (profile.empty() || profile == "production") return;

    auto& pipeline = args.pipeline;
    auto& quality = pipeline.quality;

    const auto set_if_default = [&](std::string_view flag, auto&& setter) {
        if (!is_explicit(flag)) setter();
    };
    const bool motion_blur_explicit =
        is_explicit("--motion-blur") || is_explicit("--motion-blur-mode");

    if (profile == "draft") {
        set_if_default("--tile-size", [&] { pipeline.tile_size = 256; });
        if (!motion_blur_explicit) quality.motion_blur_mode = 0;
        set_if_default("--motion-blur-samples", [&] { quality.motion_blur_samples = 2; });
        set_if_default("--warmup-framebuffers", [&] { pipeline.warmup_framebuffers = 1; });
        set_if_default("--program-cache-tune", [&] { pipeline.program_cache_tune = false; });
    } else if (profile == "preview") {
        set_if_default("--tile-size", [&] { pipeline.tile_size = 128; });
        if (!motion_blur_explicit) quality.motion_blur_mode = 1;
        set_if_default("--motion-blur-samples", [&] { quality.motion_blur_samples = 4; });
        set_if_default("--warmup-framebuffers", [&] { pipeline.warmup_framebuffers = 2; });
    } else if (profile == "maximum") {
        set_if_default("--tile-size", [&] { pipeline.tile_size = 0; });
        if (!motion_blur_explicit) quality.motion_blur_mode = 1;
        set_if_default("--motion-blur-samples", [&] { quality.motion_blur_samples = 16; });
        set_if_default("--warmup-framebuffers", [&] { pipeline.warmup_framebuffers = 4; });
        set_if_default("--program-cache-tune", [&] { pipeline.program_cache_tune = true; });
    }
}

inline bool is_render_profile(std::string_view profile) noexcept {
    return profile == "draft" || profile == "preview" ||
           profile == "production" || profile == "maximum";
}

} // namespace chronon3d::cli
