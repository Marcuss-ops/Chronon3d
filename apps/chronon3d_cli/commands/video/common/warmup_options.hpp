#pragma once

// ---------------------------------------------------------------------------
// warmup_options.hpp
//
/// @file    warmup_options.hpp
/// @brief   Focused options for renderer pre-warming before video export.
///
/// Extracted from FfmpegExportOptions — warmup is a cross-cutting concern
/// that applies to all sink types (pipe, png, null, raw).
// ---------------------------------------------------------------------------

#include <cstddef>

namespace chronon3d::cli {

/// Renderer warmup options.
///
/// Controls whether the renderer pre-allocates framebuffers, primes caches,
/// and/or renders a dummy frame before the main export loop.
struct RenderWarmupOptions {
    /// Enable renderer warmup (pre-alloc + optional dummy frame).
    bool warmup_renderer{false};

    /// Number of framebuffers to preallocate during warmup.
    size_t warmup_framebuffers{2};

    /// Render a dummy frame 0 to prime all caches before the export.
    bool warmup_dummy_frame{false};

    /// Return true when any warmup option is active.
    [[nodiscard]] bool active() const noexcept {
        return warmup_renderer || warmup_framebuffers > 0 || warmup_dummy_frame;
    }
};

} // namespace chronon3d::cli
