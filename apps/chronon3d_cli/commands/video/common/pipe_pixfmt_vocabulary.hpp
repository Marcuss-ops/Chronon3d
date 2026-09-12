#pragma once

// ---------------------------------------------------------------------------
// pipe_pixfmt_vocabulary.hpp — CLI projection of the canonical pipe pixel
// format vocabulary.
//
// The knowledge ("which names exist, what an unknown name means") lives in ONE
// place: `chronon3d::runtime::try_parse_pixel_format()` in
// <chronon3d/runtime/frame_format.hpp>. This header only projects that
// authority into the shapes a CLI validator needs, so the render command
// (`chronon3d_cli_render`) and the video-export commands
// (`chronon3d_cli_video_export`) share one list instead of each hardcoding
// their own `{"nv12","p010"}` / `{"rgba","yuv420p","nv12","p010"}` literal.
//
// Header-only on purpose: `chronon3d_cli_render` cannot link
// `chronon3d_cli_video_export` (the dependency runs the other way), so a
// compiled helper would either invert the target graph or force a second
// copy of the list.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/frame_format.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cli {

/// Names accepted by every pipe-pixel-format option (host-frame route).
[[nodiscard]] inline std::vector<std::string> pipe_pixfmt_source_names() {
    std::vector<std::string> names;
    for (const auto name : runtime::pipe_pixel_format_names()) {
        names.emplace_back(name);
    }
    return names;
}

/// Names accepted where the value selects a CUDA/pipe *surface* format
/// (render plan, daemon spec). Derived from the same authority via
/// `pixel_format_is_cuda_surface`, never re-typed.
[[nodiscard]] inline std::vector<std::string> pipe_pixfmt_cuda_surface_names() {
    std::vector<std::string> names;
    for (const auto name : runtime::pipe_pixel_format_names()) {
        const auto parsed = runtime::try_parse_pixel_format(name);
        if (parsed && runtime::pixel_format_is_cuda_surface(*parsed)) {
            names.emplace_back(name);
        }
    }
    return names;
}

/// Canonical validation for a non-empty pipe pixel format string.
/// An empty string means "no explicit request" and is accepted.
[[nodiscard]] inline bool pipe_pixfmt_is_valid_source(std::string_view name) {
    if (name.empty()) return true;
    return runtime::try_parse_pixel_format(name).has_value();
}

/// Canonical validation for a CUDA/pipe surface format. An empty string means
/// "no explicit request" and is accepted.
[[nodiscard]] inline bool pipe_pixfmt_is_valid_cuda_surface(std::string_view name) {
    if (name.empty()) return true;
    const auto parsed = runtime::try_parse_pixel_format(name);
    return parsed.has_value() && runtime::pixel_format_is_cuda_surface(*parsed);
}

} // namespace chronon3d::cli
