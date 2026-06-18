#pragma once

// ---------------------------------------------------------------------------
// frame_state_commit.hpp
//
// Saves per-frame render state for the next frame's reuse check, updates
// telemetry counters, manages ping-pong buffer swap, and stores the cached
// compiled graph for incremental reuse.
// ===========================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/renderer_state.hpp>
#include "scene_internal.hpp"
#include "scene_fingerprint.hpp"
#include <memory>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph {

/// Aggregate timing data for a single render_scene_via_graph() invocation.
/// Populated by the orchestrator and committed to telemetry counters here.
struct FrameTimings {
    double resolve_ms{0.0};
    double dirty_ms{0.0};
    double graph_ms{0.0};
    double exec_ms{0.0};
    double total_graph_ms{0.0};
};

/// Record phase timing into telemetry counters.
void record_frame_timings(
    RenderCounters* counters,
    const FrameTimings& timings);

/// Ensure ping-pong framebuffers are sized for the current resolution.
/// Must be called before graph build/reuse for ALL code paths.
/// transform_scratch.slot_view() is assigned directly in scene.cpp.
void setup_pingpong_buffers(
    SoftwareRenderer* sw_renderer,
    int width,
    int height);

/// Commit the completed frame's state for reuse by the next frame.
/// This includes:
///   - Caching the compiled graph
///   - Swapping ping-pong buffers or assigning fb directly
///   - Saving fingerprints, camera state, and layer bboxes
void commit_frame_state(
    SoftwareRenderer* sw_renderer,
    Frame frame,
    const Camera2_5D& camera,
    CompiledFrameGraph&& compiled,
    std::shared_ptr<Framebuffer>& fb_shared,
    const detail::LayerResolutionResult& resolved,
    const FrameFingerprints& fingerprints,
    detail::DirtyRectOutput& dirty_out,
    int width,
    int height);

/// Output-path helpers for diagnostics plan files.
[[nodiscard]] std::string format_plan_output_path(std::string pattern, Frame frame);
bool write_plan_output_file(const std::string& path, const std::string& contents);

} // namespace chronon3d::graph
