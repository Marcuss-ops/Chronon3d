#pragma once

#include <chronon3d/math/raster_utils.hpp>

#include <cstdint>
#include <optional>

namespace chronon3d::graph::detail {

enum class ClearExecutionPath {
    Software,
    NativeSurface,
    Reject,
};

/// Decide whether ClearNode may attempt the native transparent surface path,
/// or must fail before building a software frame in strict native mode.
/// Allocation/initialization failures remain a runtime concern of execute().
[[nodiscard]] ClearExecutionPath resolve_clear_execution_path(
    bool require_native_gpu,
    bool backend_supports_native_surface,
    bool backend_available,
    bool surface_registry_available) noexcept;

struct DirtyRectClearPlan {
    bool use_dirty_rects{false};
    bool skip_clear{false};
    bool empty_clip{false};
    bool full_clip{false};
    bool prefer_full_clear{false};
    std::uint64_t clear_pixels{0};
    float clip_fraction{0.0f};
};

/// Resolve dirty-rect eligibility and geometry without touching framebuffer
/// state. Pixel copying/clearing and telemetry remain in ClearNode::execute.
[[nodiscard]] DirtyRectClearPlan resolve_dirty_rect_clear_plan(
    bool software_backend,
    bool reuse_previous_framebuffer,
    bool has_write_framebuffer,
    bool has_previous_framebuffer,
    bool skip_initial_clear,
    const std::optional<raster::BBox>& clip,
    int frame_width,
    int frame_height) noexcept;

} // namespace chronon3d::graph::detail
