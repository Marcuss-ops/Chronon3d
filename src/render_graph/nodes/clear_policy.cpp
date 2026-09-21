#include "clear_policy.hpp"

#include <algorithm>

namespace chronon3d::graph::detail {

ClearExecutionPath resolve_clear_execution_path(
    bool require_native_gpu,
    bool backend_supports_native_surface,
    bool backend_available,
    bool surface_registry_available) noexcept {
    const bool native_requested =
        require_native_gpu || backend_supports_native_surface;
    if (native_requested && backend_available && surface_registry_available) {
        return ClearExecutionPath::NativeSurface;
    }
    return require_native_gpu
        ? ClearExecutionPath::Reject
        : ClearExecutionPath::Software;
}

DirtyRectClearPlan resolve_dirty_rect_clear_plan(
    const bool software_backend,
    const bool reuse_previous_framebuffer,
    const bool has_write_framebuffer,
    const bool has_previous_framebuffer,
    const bool skip_initial_clear,
    const std::optional<raster::BBox>& clip,
    const int frame_width,
    const int frame_height) noexcept {
    DirtyRectClearPlan plan;
    plan.use_dirty_rects = software_backend && reuse_previous_framebuffer &&
        has_write_framebuffer && has_previous_framebuffer;
    plan.empty_clip = clip && clip->is_empty();
    plan.full_clip = !clip ||
        (clip->x0 <= 0 && clip->y0 <= 0 &&
         clip->x1 >= frame_width && clip->y1 >= frame_height);
    const auto clip_width = clip
        ? std::max(0, clip->x1 - clip->x0) : std::max(0, frame_width);
    const auto clip_height = clip
        ? std::max(0, clip->y1 - clip->y0) : std::max(0, frame_height);
    plan.clear_pixels = static_cast<std::uint64_t>(clip_width) *
        static_cast<std::uint64_t>(clip_height);
    const auto frame_pixels = static_cast<std::uint64_t>(std::max(0, frame_width)) *
        static_cast<std::uint64_t>(std::max(0, frame_height));
    plan.clip_fraction = frame_pixels > 0
        ? static_cast<float>(plan.clear_pixels) / static_cast<float>(frame_pixels)
        : 0.0f;
    plan.prefer_full_clear = plan.use_dirty_rects && !plan.empty_clip &&
        !plan.full_clip && plan.clip_fraction > 0.5f;
    plan.skip_clear = skip_initial_clear ||
        (plan.use_dirty_rects && plan.empty_clip);
    return plan;
}

} // namespace chronon3d::graph::detail
