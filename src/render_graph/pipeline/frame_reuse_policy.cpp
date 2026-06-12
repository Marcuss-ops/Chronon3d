#include "frame_reuse_policy.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

FrameReuseResult evaluate_resolved_scene_reuse(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    int width,
    int height,
    bool diagnostics_enabled)
{
    FrameReuseResult result;

    if (!sw_renderer ||
        !sw_renderer->buffer_ring().prev_framebuffer() ||
        sw_renderer->buffer_ring().prev_framebuffer()->width() != width ||
        sw_renderer->buffer_ring().prev_framebuffer()->height() != height)
    {
        return result;
    }

    const auto& history = sw_renderer->frame_history();
    const bool frame_eligible = (history.prev_frame == frame - 1 || history.prev_frame == frame);
    if (!frame_eligible) return result;

    const bool cam_changed = detail::camera_changed(
        camera, &history.prev_camera, history.prev_camera_valid);

    if (!cam_changed && history.prev_scene_fingerprint == fps.combined_fp) {
        // All conditions met — return previous framebuffer
        sw_renderer->mark_fast_path_reused(frame, camera, fps.combined_fp);

        if (sw_renderer->counters()) {
            sw_renderer->counters()->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
            sw_renderer->counters()->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
            sw_renderer->counters()->clear_skipped_pixels.fetch_add(
                static_cast<uint64_t>(width) * height,
                std::memory_order_relaxed);
        }

        if (diagnostics_enabled) {
            spdlog::info("[resolved-reuse] frame={} combined_fingerprint_match=1",
                static_cast<int>(frame));
        }

        result.can_reuse = true;
        result.framebuffer = sw_renderer->buffer_ring().prev_framebuffer();
        return result;
    }

    return result;
}

FrameReuseResult evaluate_static_scene_fastpath(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    bool scene_structure_unchanged,
    bool scene_is_static,
    bool static_cam_changed,
    const FrameFingerprints& prev_fps,
    int width,
    int height,
    bool diagnostics_enabled)
{
    FrameReuseResult result;
    result.scene_structure_unchanged = scene_structure_unchanged;
    result.scene_is_static = scene_is_static;
    result.static_cam_changed = static_cam_changed;
    result.current_static_fp = fps.static_fp;
    result.current_active_at_fp = fps.active_at_fp;
    result.current_structure_fp = fps.structure_fp;
    result.current_combined_fp = fps.combined_fp;

    if (!sw_renderer ||
        !sw_renderer->buffer_ring().prev_framebuffer() ||
        sw_renderer->buffer_ring().prev_framebuffer()->width() != width ||
        sw_renderer->buffer_ring().prev_framebuffer()->height() != height)
    {
        return result;
    }

    const auto& history = sw_renderer->frame_history();

    // Frame reuse allowed: same frame, or consecutive with static scene
    const bool frame_reuse = (history.prev_frame == frame) ||
        (scene_is_static && history.prev_frame == frame - 1);

    if (!frame_reuse) return result;

    // Active-at must be unchanged
    const bool active_at_unchanged = (fps.active_at_fp != 0) &&
        (fps.active_at_fp == history.prev_active_at_fingerprint);

    if (scene_structure_unchanged &&
        !static_cam_changed &&
        active_at_unchanged &&
        history.prev_static_scene_fingerprint != 0 &&
        fps.static_fp == history.prev_static_scene_fingerprint)
    {
        sw_renderer->mark_fast_path_reused(frame, camera, fps.combined_fp);

        if (sw_renderer->counters()) {
            sw_renderer->counters()->dirty_union_area_pixels.store(0, std::memory_order_relaxed);
            sw_renderer->counters()->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
            sw_renderer->counters()->clear_skipped_pixels.fetch_add(
                static_cast<uint64_t>(width) * height,
                std::memory_order_relaxed);
        }

        if (diagnostics_enabled) {
            spdlog::info("[static-fastpath] frame={} static_fingerprint_match=1",
                static_cast<int>(frame));
        }

        result.can_reuse = true;
        result.framebuffer = sw_renderer->buffer_ring().prev_framebuffer();
        return result;
    }

    return result;
}

FrameReuseResult evaluate_empty_dirty_reuse(
    SoftwareRenderer* sw_renderer,
    const Scene& scene,
    Frame frame,
    const Camera2_5D& camera,
    const FrameFingerprints& fps,
    const detail::DirtyRectOutput& dirty_out,
    const RenderSettings& settings,
    int width,
    int height,
    bool diagnostics_enabled)
{
    FrameReuseResult result;

    const bool fast_path_reuse = sw_renderer &&
        settings.dirty.enabled &&
        dirty_out.dirty_rect &&
        dirty_out.dirty_rect->is_empty() &&
        sw_renderer->buffer_ring().prev_framebuffer() &&
        sw_renderer->buffer_ring().prev_framebuffer()->width() == width &&
        sw_renderer->buffer_ring().prev_framebuffer()->height() == height;

    if (fast_path_reuse) {
        if (diagnostics_enabled) {
            spdlog::info("[dirty-debug] frame={} fast_path_reuse=1", static_cast<int>(frame));
        }

        sw_renderer->dirty_telemetry().last_dirty_area_ratio = 0.0;
        sw_renderer->update_dirty_telemetry(true, dirty_out.dirty_rect, false, true, false);

        result.can_reuse = true;
        result.framebuffer = sw_renderer->buffer_ring().prev_framebuffer();
        result.current_static_fp = fps.static_fp;
        result.current_active_at_fp = fps.active_at_fp;
        result.current_combined_fp = fps.combined_fp;
        result.current_structure_fp = fps.structure_fp;
        return result;
    }

    return result;
}

} // namespace chronon3d::graph
