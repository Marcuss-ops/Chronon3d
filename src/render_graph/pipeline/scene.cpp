// ---------------------------------------------------------------------------
// scene.cpp — Thin orchestrator for render_scene_via_graph() (Azione 19)
//
// Phase map (extracted to focused TUs):
//   0  scene_context_setup.cpp       — context + assets + camera + DebugConfig
//   1-3 tile_execution_policy.cpp    — canonical reuse plan + fingerprints
//   4-6  scene_dirty.cpp             — dirty rect + metrics
//   7   tile_execution_policy.cpp    — empty dirty-rect reuse in plan
//   8   graph_cache_coordinator.cpp  — build or reuse compiled graph
//   9   tile_execution_coordinator.cpp — tile decision + execute + fb
//   10  pool_preallocation.cpp       — preallocation
//   11  frame_timing_recorder.cpp    — phase timing telemetry
//   12  frame_state_commit.cpp       — commit state for next frame
//   n   scene_native_output.cpp      — native output sync + encode residency
// ===========================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/compiler/compiler_pass_pipeline.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>

#include <chrono>

#include "helpers.hpp"
#include "scene_internal.hpp"
#include "scene_context_setup.hpp"        // Azione 19 — Phase 0 helper
#include "scene_temporal_policy.hpp"
#include "scene_native_output.hpp"        // native output sync + residency
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include "../core/scene_fingerprint.hpp"
#include "tile_execution_policy.hpp"
#include "graph_cache_coordinator.hpp"
#include "pool_preallocation.hpp"
#include "frame_state_commit.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include "dirty_telemetry_reporter.hpp"
#include "tile_execution_coordinator.hpp"
#include "frame_timing_recorder.hpp"
#include <chronon3d/core/scope/execution_scope.hpp>  // PR 6.2 — root scope

#include <spdlog/spdlog.h>
#include <cassert>
#include <cstring>
#include <vector>
#include "temporal_render_context.hpp"
#include "../nodes/native_surface.hpp"
#include "scene_temporal_execution.hpp"

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> render_scene_via_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    float fps,
    std::string_view diagnostic_label,
    chronon3d::SoftwareRenderer* sw_sidecar,
    chronon3d::runtime::FrameExecution* execution,
    const chronon3d::compiler::FrameParameterBlock* frame_parameters,
    std::uint64_t program_fingerprint)
{
    return render_scene_via_graph_temporal(
        backend, node_cache, scene, camera, width, height, frame, frame_time,
        settings, registry, video_decoder, fps, diagnostic_label, sw_sidecar,
        nullptr, execution, frame_parameters, program_fingerprint);
}

} // namespace chronon3d::graph
