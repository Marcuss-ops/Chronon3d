#pragma once

#include "pipe_export_session.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace chronon3d::cli::detail {

struct FrameProfileSample {
    uint64_t timeline_eval_us{0};
    uint64_t text_us{0};
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
    uint64_t node_lookup_us{0};
};

struct FrameTimingProjection {
    double node_lookup_ms{0.0};
    chronon3d::telemetry::FrameRenderBreakdown breakdown{};
    chronon3d::telemetry::FrameImageTiming image_timing{};
    chronon3d::telemetry::FrameTextTiming text_timing{};
};

[[nodiscard]] FrameProfileSample sample_frame_profile(const RenderLoopContext& ctx);
[[nodiscard]] FrameTimingProjection project_frame_timings(
    const FrameProfileSample& before,
    const FrameProfileSample& after,
    double frame_ms);

struct NativeSurfacePrep {
    RenderSettings video_settings{};
    runtime::FrameExecutionSlotRing::SlotLease slot;
};

struct RenderOutcome {
    std::shared_ptr<Framebuffer> fb;
    FrameTimingProjection timing{};
    std::chrono::steady_clock::time_point wall_start{};
    double frame_ms{0.0};
    double dirty_ratio{0.0};
    bool dirty_rect_enabled{false};
    std::optional<raster::BBox> dirty_rect;
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

[[nodiscard]] NativeSurfacePrep prepare_frame(
    const RenderLoopContext& ctx,
    const RenderSettings& settings,
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
    PipeExportStatus& status);
void finalize_render_session(PipeExportStatus& status, Frame current_frame, Frame end);

} // namespace chronon3d::cli::detail
