// ═══════════════════════════════════════════════════════════════════════════
// Canonical graph adapter for the typed CompiledProgram execution path.
//
//   CompiledProgram → SceneMaterializer → render_scene_via_graph()
//
// The legacy Composition executor was removed. This is the canonical graph
// entry used by CompiledProgram execution and by test fixtures that still
// author a Scene directly.
//
// The Fase C3 deprecated wrappers (`RenderPipeline::render_scene` x2,
// `RenderPipeline::debug_graph`) have been removed — migrate to `render()`.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace chronon3d {
class SoftwareRenderer;  // sw_sidecar parameter for render_composition_frame
struct RenderSettings;
namespace compiler { struct FrameParameterBlock; }
namespace runtime { class FrameExecution; }
}

namespace chronon3d::graph {

/// Core 12-phase scene-graph pipeline (canonical graph execution path).
/// Phases: context→fingerprints→resolved-scene reuse→static fast-path→
/// resolve layers→dirty rect→empty-dirty reuse→ping-pong setup→
/// build/reuse graph→preallocate→execute tile/fallback→telemetry→commit.
///
/// Called by CompiledProgram execution and directly by test infrastructure.
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
    std::string_view diagnostic_label = "scene",
    chronon3d::SoftwareRenderer* sw_sidecar = nullptr,
    chronon3d::runtime::FrameExecution* execution = nullptr,
    const chronon3d::compiler::FrameParameterBlock* frame_parameters = nullptr,
    std::uint64_t program_fingerprint = 0
);

/**
 * Generates a Graphviz DOT representation of the compiled render graph for diagnostic purposes.
 */
std::string debug_scene_graph(
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
    float fps
);

// ── Graph diagnostics ──────────────────────────────────────────────────────

struct SceneGraphStats {
    int scene_layers{0};
    std::size_t total_nodes{0};
    GraphNodeId output_id{k_invalid_node};
    std::size_t source_nodes{0};
    std::size_t transform_nodes{0};
    std::size_t effect_nodes{0};
    std::size_t mask_nodes{0};
    std::size_t composite_nodes{0};
    std::size_t adjustment_nodes{0};
    std::size_t precomp_nodes{0};
    std::size_t video_nodes{0};
    std::size_t motion_blur_nodes{0};
    std::size_t other_nodes{0};
    std::size_t cacheable_nodes{0};
    double build_ms{0.0};
    double execute_ms{0.0};
    double total_ms{0.0};
    std::string dot;
};

/**
 * Builds the render graph for a scene, counts nodes by kind, optionally executes
 * it for cache/timing diagnostics, and optionally generates the DOT string.
 */
SceneGraphStats analyze_scene_graph(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    i32 width, i32 height,
    Frame frame, f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    media::MediaFrameProvider* video_decoder,
    float fps,
    bool execute = true,
    bool include_dot = false
);

} // namespace chronon3d::graph
