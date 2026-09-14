#pragma once

#include "pipe_export_session.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
namespace chronon3d::cli::detail {

struct FrameProfileSample {
    uint64_t timeline_eval_us{0};
    uint64_t text_us{0};
    uint64_t text_prepare_per_frame_us{0};
    uint64_t graph_prepare_us{0};
    uint64_t graph_execute_us{0};
    uint64_t compositing_us{0};
    uint64_t effects_us{0};
    uint64_t surface_us{0};
    uint64_t overhead_us{0};
    uint64_t image_draw_us{0};
    uint64_t image_draw_count{0};
    uint64_t text_shaping_ms{0};
    uint64_t text_bidi_ms{0};
    uint64_t text_layout_ms{0};
    uint64_t text_glyph_lookup_us{0};
    uint64_t text_raster_ms{0};
    uint64_t text_atlas_upload_us{0};
    uint64_t text_draw_us{0};
    uint64_t text_styled_glyph_build_us{0};
    uint64_t text_atlas_pack_us{0};
    uint64_t text_atlas_pixel_copy_us{0};
    uint64_t text_atlas_sha256_us{0};
    uint64_t text_atlas_cache_acquire_us{0};
    uint64_t text_prepared_batch_build_count{0};
    uint64_t text_prepared_batch_reuse_count{0};
    uint64_t node_lookup_us{0};
    uint64_t gpu_execute_us{0};
    uint64_t gpu_submit_cpu_us{0};
    uint64_t gpu_wait_cpu_us{0};
    uint64_t gpu_drain_wait_us{0};
    uint64_t gpu_submissions{0};
    uint64_t gpu_drain_wait_count{0};
};

struct FrameTimingProjection {
    double node_lookup_ms{0.0};
    chronon3d::telemetry::FrameRenderBreakdown breakdown{};
    chronon3d::telemetry::FrameImageTiming image_timing{};
    chronon3d::telemetry::FrameTextTiming text_timing{};
    chronon3d::telemetry::FrameGpuTiming gpu_timing{};
};

[[nodiscard]] FrameProfileSample sample_frame_profile(const RenderLoopContext& ctx);
[[nodiscard]] FrameTimingProjection project_frame_timings(
    const FrameProfileSample& before,
    const FrameProfileSample& after,
    double frame_ms);

/// Per-frame native-surface preparation result.
///
/// It deliberately carries ONLY the acquired slot lease: the derived render
/// settings live in the caller-owned scratch the loop mutates in place, so a
/// frame never copies the whole RenderSettings (which owns three std::string
/// members plus the render budget) just to override four scalars.
struct NativeSurfacePrep {
    runtime::GpuSlotPool::Lease slot;
};

struct RenderOutcome {
    std::shared_ptr<Framebuffer> fb;
    FrameTimingProjection timing{};
    std::chrono::steady_clock::time_point wall_start{};
    double frame_ms{0.0};
    double dirty_ratio{0.0};
    bool dirty_rect_enabled{false};
    std::optional<raster::BBox> dirty_rect;
    std::vector<raster::BBox> dirty_regions;
    bool tile_execution_used{false};
    bool fast_path_reused{false};
    bool graph_reused{false};
};

struct EncodeOutcome {
    bool pushed{false};
    bool source_residency_failed{false};
    RenderFramePackage package;
    double wait_ms{0.0};
    uint64_t node_cache_hits_after{0};
};

/// Derives this frame's native-surface contract into `frame_settings` (mutated
/// in place: one scratch instance lives for the whole job) and acquires the
/// frame's slot lease. Every derived field is written or cleared on each call,
/// so a value can never leak from the previous frame.
[[nodiscard]] NativeSurfacePrep prepare_frame(
    const RenderLoopContext& ctx,
    RenderSettings& frame_settings,
    Frame current_frame);
[[nodiscard]] RenderOutcome render_frame(
    const RenderLoopContext& ctx,
    const RenderSettings& video_settings,
    Frame current_frame);
[[nodiscard]] EncodeOutcome encode_frame(
    const RenderLoopContext& ctx,
    std::shared_ptr<Framebuffer> fb,
    const RenderSettings& video_settings,
    std::shared_ptr<FramebufferArena> current_arena,
    NativeSurfacePrep prep,
    Frame current_frame,
    PipeExportStatus& status,
    std::span<const raster::BBox> dirty_regions = {});
void finalize_render_session(PipeExportStatus& status, Frame current_frame, Frame end);

} // namespace chronon3d::cli::detail
